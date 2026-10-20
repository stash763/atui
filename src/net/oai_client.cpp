// Definitions for the OpenAI-compatible streaming HTTP client.
#include "oai_client.h"
#include "json_extract.h"
#include "provider.h"

#include "core/strutil.h"      // toString, startsWith: Watcom has neither
#include "oai_body.h"          // buildBody, mergeToolFrags
#include "sse.h"               // SseStream
#ifdef ATUI_DEBUG_KEYS
#include "dbg.h"
#endif

#include <curl/curl.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdexcept>

namespace atui {
namespace net {

// State carried by the curl write callback.  The framing -- bytes to lines to
// `data:` payloads -- is SseStream's, and the meaning of a payload is
// applyStreamDelta's, which is the same code the event-loop transport runs: two
// transports may disagree about sockets, not about what the model said.
struct CurlSse : public SseStream::Sink, public DeltaSink {
    SseStream sse;
    TokenCallback cb;
    void *ctx;
    OaiResponse *out;      // may be null

    virtual void onDelta( const std::string &text, bool thinking )
    {
        if( cb )
            cb( text.c_str(), thinking, ctx );
    }
    virtual void onPayload( const std::string &payload )
    {
        applyStreamDelta( payload, out, this );
    }
    virtual void onDone() {}
};


namespace {


// curl write callback: hand the bytes to the SSE framing and keep curl happy by
// reporting all of them consumed.  Returning anything else aborts the transfer, so
// a parse problem is recorded and reported at the end rather than acted on here.
size_t writeData( char *ptr, size_t size, size_t nmemb, void *op )
{
    CurlSse *sp = static_cast< CurlSse * >( op );
    sp->sse.feed( ptr, size * nmemb, *sp );
    return size * nmemb;
}

} // anonymous namespace

bool OaiClient::request( const std::string &baseUrl,
                         const std::string &apiKey,
                         const OaiRequest &req,
                         TokenCallback cb,
                         void *ctx,
                         OaiResponse *out,
                         std::string *err )
{
    if( err ) err->clear();

    CURL *curl = curl_easy_init();
    if( !curl )
    {
        if( err ) *err = "could not start a request (curl init failed)";
        return false;
    }
    // Signals are not for curl to use in a program like this one. Every request
    // runs on a worker thread (oai_chat_model detaches one per turn), and curl's
    // default is to install a SIGALRM handler and longjmp out of it to time a name
    // resolution - in whichever thread the alarm happens to land, which in a TUI
    // is a crash nobody can reproduce. Some builds of it also hold SIGPIPE ignored
    // while a transfer is up, and an *ignored* signal is inherited by every command
    // atui runs: that is how `yes | head` starts misbehaving through atui and not
    // at the shell. What the signal bought is a timeout on name resolution, and
    // `CURLOPT_CONNECTTIMEOUT` below covers that with a timer. The residual
    // difference is that a blocking resolver cannot be interrupted mid-lookup, so a
    // broken DNS server can hold a request a little longer than it should: a slow
    // request, in exchange for not crashing in the middle of a streamed answer.
    curl_easy_setopt( curl, CURLOPT_NOSIGNAL, 1L );

    std::string url;
    if( baseUrl.empty() )
    {
        if( err ) *err = "no server URL set";
        curl_easy_cleanup( curl );   // the handle exists at this point
        return false;
    }
    url = baseUrl;
    if( !url.empty() && url.back() != '/' )
        url += "/";
    url += "chat/completions";

    CurlSse sp;
    sp.cb = cb;
    sp.ctx = ctx;
    sp.out = out;
    sp.sse = SseStream();

    struct curl_slist *headers = 0;
    headers = curl_slist_append( headers, "Content-Type: application/json" );
    if( !apiKey.empty() )
    {
        std::string auth = std::string( "Authorization: Bearer " ) + apiKey;
        headers = curl_slist_append( headers, auth.c_str() );
    }
    if( req.stream )
    {
        headers = curl_slist_append( headers, "Accept: text/event-stream" );
    }

    std::string body = buildBody( req );

    curl_easy_setopt( curl, CURLOPT_URL, url.c_str() );
    if( headers )
        curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );
    curl_easy_setopt( curl, CURLOPT_POSTFIELDS, body.c_str() );
    curl_easy_setopt( curl, CURLOPT_POST, 1L );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, writeData );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, &sp );
    curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );
    // The whole request - connect, thinking, and every streamed token - has to
    // finish inside this (Options > Request timeout). A model that is still
    // reasoning has the connection open, so a short limit here is what makes a
    // long prompt "time out". A request with its own budget (the permission
    // agent) is measured in seconds instead: waiting minutes for a decision
    // would be the wrong trade on purpose.
    curl_easy_setopt( curl, CURLOPT_TIMEOUT,
                      req.timeoutSeconds > 0
                          ? (long) req.timeoutSeconds
                          : (long) requestMinutes() * 60L );
    curl_easy_setopt( curl, CURLOPT_CONNECTTIMEOUT, 30L );

    // Allow HTTP to be used (local llama.cpp server is plain HTTP).
    curl_easy_setopt( curl, CURLOPT_PROTOCOLS,
                      CURLPROTO_HTTP | CURLPROTO_HTTPS );
    curl_easy_setopt( curl, CURLOPT_SSL_VERIFYPEER, 1L );

#ifdef ATUI_DEBUG_KEYS
    atui::net::dbg( "request url=%s body_len=%zu", url.c_str(), body.size() );
#endif
    CURLcode res = curl_easy_perform( curl );
    curl_slist_free_all( headers );
    curl_easy_cleanup( curl );

#ifdef ATUI_DEBUG_KEYS
    atui::net::dbg( "request done res=%d (msg=%s) sp_done=%d",
                    (int)res, curl_easy_strerror( res ), (int)sp.sse.done() );
#endif

    if( res != CURLE_OK && err )
    {
        // A timeout is the one failure the user can do something about, so say
        // how long it waited and where that is set.
        if( res == CURLE_OPERATION_TIMEDOUT )
        {
            char msg[ 128 ];
            snprintf( msg, sizeof msg,
                      "gave up after %d min waiting for the server "
                      "(Options > Settings)", requestMinutes() );
            *err = msg;
        }
        else
        {
            *err = curl_easy_strerror( res );
        }
    }

    return res == CURLE_OK;
}

namespace {
size_t writeStr( char *ptr, size_t size, size_t nmemb, void *op )
{
    size_t n = size * nmemb;
    static_cast<std::string *>( op )->append( ptr, n );
    return n;
}
} // anonymous namespace

bool fetchModels( const std::string &baseUrl,
                  const std::string &apiKey,
                  std::vector<std::string> &models,
                  std::string &err )
{
    models.clear();
    err.clear();
    if( baseUrl.empty() ) { err = "no server URL set"; return false; }

    CURL *curl = curl_easy_init();
    if( !curl ) { err = "curl init failed"; return false; }
    // Same reason as in OaiClient::request: curl does not get to install handlers
    // or ignore signals in this process, and this request is made from the UI
    // thread.
    curl_easy_setopt( curl, CURLOPT_NOSIGNAL, 1L );

    std::string url = baseUrl;
    if( !url.empty() && url.back() != '/' ) url += "/";
    url += "models";

    struct curl_slist *headers = 0;
    if( !apiKey.empty() )
    {
        std::string auth = std::string( "Authorization: Bearer " ) + apiKey;
        headers = curl_slist_append( headers, auth.c_str() );
    }

    std::string body;
    curl_easy_setopt( curl, CURLOPT_URL, url.c_str() );
    curl_easy_setopt( curl, CURLOPT_HTTPGET, 1L );
    if( headers )
        curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, writeStr );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, &body );
    curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );
    curl_easy_setopt( curl, CURLOPT_TIMEOUT, 20L );
    curl_easy_setopt( curl, CURLOPT_CONNECTTIMEOUT, 10L );
    curl_easy_setopt( curl, CURLOPT_PROTOCOLS,
                      CURLPROTO_HTTP | CURLPROTO_HTTPS );
    curl_easy_setopt( curl, CURLOPT_SSL_VERIFYPEER, 1L );

    CURLcode res = curl_easy_perform( curl );
    long code = 0;
    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &code );
    curl_slist_free_all( headers );
    curl_easy_cleanup( curl );

    if( res != CURLE_OK )
    {
        err = std::string( "request failed: " ) + curl_easy_strerror( res );
        return false;
    }
    if( code < 200 || code >= 300 )
    {
        err = "server returned HTTP " + core::toString( code );
        return false;
    }
    models = modelIds( body );
    return true;
}

} // namespace atui::net
} // namespace atui
