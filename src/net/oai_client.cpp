// Definitions for the OpenAI-compatible streaming HTTP client.
#include "oai_client.h"
#include "json_extract.h"
#include "provider.h"
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

// State carried by the curl write callback.
struct SseParser {
    std::string buf;       // raw data not yet parsed
    TokenCallback cb;
    void *ctx;
    OaiResponse *out;      // may be null
    bool done;
};

namespace {

// Merge a streaming delta.tool_calls fragment batch into the response, keyed
// by the fragment index (id/name arrive once, arguments arrive in pieces).
void mergeToolFrags( OaiResponse &resp, const std::vector<ToolCallFrag> &frags )
{
    for( size_t i = 0; i < frags.size(); ++i )
    {
        const ToolCallFrag &f = frags[i];
        int idx = f.index < 0 ? 0 : f.index;
        while( (int) resp.toolCalls.size() <= idx )
            resp.toolCalls.push_back( OaiToolCall() );
        OaiToolCall &tc = resp.toolCalls[idx];
        if( !f.id.empty() ) tc.id = f.id;
        if( !f.name.empty() && tc.name.empty() ) tc.name = f.name;
        tc.arguments += f.arguments;
    }
}

} // anonymous namespace

namespace {

// Build the JSON request body for /v1/chat/completions.
std::string buildBody( const OaiRequest &req )
{
    std::string body;
    body.reserve( 256 );
    body += "{\"model\":\"";
    body += jsonEscape( req.model );
    body += "\",\"stream\":";
    body += ( req.stream ? "true" : "false" );
    body += ",\"temperature\":";
    body += std::to_string( req.temperature );

    body += ",\"messages\":[";
    for( size_t i = 0; i < req.messages.size(); ++i )
    {
        const OaiMessage &m = req.messages[i];
        if( i > 0 ) body += ", ";
        body += "{\"role\":\"";
        body += jsonEscape( m.role );
        body += "\",\"content\":";
        // An assistant turn that only asks for tools may carry no text; emit
        // null content then (some servers reject empty strings with tool_calls).
        bool nullContent = ( m.role == "assistant" && !m.toolCalls.empty() &&
                             m.content.empty() );
        if( nullContent )
            body += "null";
        else
        {
            body += "\"";
            body += jsonEscape( m.content );
            body += "\"";
        }

        // Optional thinking text carried on an assistant turn (only present
        // when the user enabled "resend thinking"). Emitted as
        // reasoning_content, the common replay convention.
        if( m.role == "assistant" && !m.reasoning.empty() )
        {
            body += ",\"reasoning_content\":\"";
            body += jsonEscape( m.reasoning );
            body += "\"";
        }

        if( m.role == "assistant" && !m.toolCalls.empty() )
        {
            body += ",\"tool_calls\":[";
            for( size_t c = 0; c < m.toolCalls.size(); ++c )
            {
                const OaiToolCall &tc = m.toolCalls[c];
                if( c ) body += ",";
                body += "{\"id\":\"";
                body += jsonEscape( tc.id );
                body += "\",\"type\":\"function\",\"function\":{\"name\":\"";
                body += jsonEscape( tc.name );
                body += "\",\"arguments\":\"";
                body += jsonEscape( tc.arguments ); // arguments is itself JSON text
                body += "\"}}";
            }
            body += "]";
        }

        if( m.role == "tool" )
        {
            body += ",\"tool_call_id\":\"";
            body += jsonEscape( m.toolCallId );
            body += "\"";
            if( !m.name.empty() )
            {
                body += ",\"name\":\"";
                body += jsonEscape( m.name );
                body += "\"";
            }
        }
        body += "}";
    }
    body += "]";

    if( !req.toolsJson.empty() )
    {
        body += ",\"tools\":";
        body += req.toolsJson;
        body += ",\"tool_choice\":\"auto\"";
    }
    body += "}";
    return body;
}

// curl write callback: accumulate data and extract complete SSE events.
size_t writeData( char *ptr, size_t size, size_t nmemb, void *op )
{
    SseParser *sp = static_cast< SseParser * >( op );
#ifdef ATUI_DEBUG_KEYS
    atui::net::dbg( "writeData: bytes=%zu buf_len=%zu", size * nmemb, sp->buf.size() );
#endif
    sp->buf.append( ptr, size * nmemb );

    while( true )
    {
        size_t nl = sp->buf.find( '\n' );
        if( nl == std::string::npos )
            break; // no complete line yet

        std::string line = sp->buf.substr( 0, nl );
        if( !line.empty() && line.back() == '\r' )
            line.pop_back();

        // Consume this line (keep the trailing incomplete one).
        sp->buf.erase( 0, nl + 1 );

        // Only "data:" lines carry payload; SSE spec concatenates data: values
        // in an event, but the OpenAI API sends one JSON object per line.
        if( line.size() >= 5 && line.compare( 0, 4, "data" ) == 0 &&
            line[4] == ':' )
        {
            size_t start = 5;
            if( start < line.size() && line[start] == ' ' )
                ++start;
            std::string payload = line.substr( start );

#ifdef ATUI_DEBUG_KEYS
            atui::net::dbg( "line data: payload_len=%zu", (size_t)payload.size() );
#endif

            if( payload == "[DONE]" )
            {
                sp->done = true;
                break;
            }
            if( payload.empty() )
                continue;

            std::string content = deltaContent( payload );
            if( !content.empty() )
            {
                if( sp->cb )
                    sp->cb( content.c_str(), false, sp->ctx );
                if( sp->out )
                    sp->out->content += content;
            }

            std::string reasoning = deltaReasoning( payload );
            if( !reasoning.empty() )
            {
                if( sp->cb )
                    sp->cb( reasoning.c_str(), true, sp->ctx );
                if( sp->out )
                    sp->out->reasoning += reasoning;
            }

            if( sp->out )
            {
                std::string fr = finishReason( payload );
                if( !fr.empty() )
                    sp->out->finishReason = fr;

                std::vector<ToolCallFrag> frags;
                if( parseToolCallFrags( payload, frags ) )
                    mergeToolFrags( *sp->out, frags );
            }
        }
        else
        {
#ifdef ATUI_DEBUG_KEYS
            atui::net::dbg( "line non-data: len=%zu first=%c",
                            (size_t)line.size(), line.empty() ? ' ' : line[0] );
#endif
        }
    }
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

    SseParser sp;
    sp.cb = cb;
    sp.ctx = ctx;
    sp.out = out;
    sp.done = false;

    struct curl_slist *headers = nullptr;
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
    // long prompt "time out".
    curl_easy_setopt( curl, CURLOPT_TIMEOUT,
                      (long) requestMinutes() * 60L );
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
                    (int)res, curl_easy_strerror( res ), (int)sp.done );
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
                      "(Options > Request timeout)", requestMinutes() );
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

    std::string url = baseUrl;
    if( !url.empty() && url.back() != '/' ) url += "/";
    url += "models";

    struct curl_slist *headers = nullptr;
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
        err = "server returned HTTP " + std::to_string( code );
        return false;
    }
    models = modelIds( body );
    return true;
}

} // namespace atui::net
} // namespace atui
