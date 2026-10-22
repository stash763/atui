// Definitions for one streaming chat completion.  See chat_request.h.
#include "chat_request.h"

#include "oai_body.h"
#include "url.h"

#include "core/strutil.h"

namespace atui {
namespace net {

namespace {

// How much of an error body is kept.  A server that is complaining usually says so
// in a sentence; a server that is streaming an error page is not worth holding.
const std::size_t kMaxErrorBody = 4096;

// How much of it is quoted back.  The rest of a JSON error is noise in an 80-column
// transcript.
const std::size_t kQuotedError = 200;

} // anonymous namespace

bool ChatRequest::begin( const std::string &baseUrl,
                         const std::string &apiKey,
                         const OaiRequest &req,
                         Sink *sink,
                         int connectSeconds,
                         int idleSeconds,
                         std::string *error )
{
    sink_ = sink;
    reported_ = false;
    ok_ = false;
    error_.clear();   // a request that is starting again has not failed yet
    status_ = 0;
    out_ = OaiResponse();
    errorBody_.clear();
    sse_ = SseStream();

    HttpEndpoint endpoint;
    std::string why;
    if( !parseHttpUrl( baseUrl, endpoint, why ) )
    {
        if( error )
            *error = why;
        return false;
    }

    const std::string accept = req.stream ? "text/event-stream" : "";
    const std::string authorization =
        apiKey.empty() ? std::string() : "Bearer " + apiKey;

    return http_.start( "POST", endpoint.host, endpoint.port,
                        joinPath( endpoint.path, "chat/completions" ),
                        "application/json", buildBody( req ), accept,
                        authorization, connectSeconds, idleSeconds,
                        this, error );
}

void ChatRequest::onStatus( int code )
{
    status_ = code;
}

void ChatRequest::onBody( const char *data, std::size_t len )
{
    // A failed status is not a stream to parse: its body is the explanation, and
    // pulling `data:` lines out of it would hide the thing the user needs to read.
    if( status_ != 0 && ( status_ < 200 || status_ >= 300 ) )
    {
        if( errorBody_.size() < kMaxErrorBody )
        {
            const std::size_t room = kMaxErrorBody - errorBody_.size();
            errorBody_.append( data, len < room ? len : room );
        }
        return;
    }
    sse_.feed( data, len, *this );
}

void ChatRequest::onPayload( const std::string &payload )
{
    applyStreamDelta( payload, &out_, this );
}

void ChatRequest::onDone()
{
    // The answer is over.  The connection is dropped rather than waited for: the
    // request asked for `Connection: close`, but a server that keeps a socket open
    // after saying `[DONE]` should not be able to hold up a finished answer.
    finish( true, std::string() );
    if( http_.busy() )
        http_.abort( "answer complete" );
}

void ChatRequest::onDelta( const std::string &text, bool thinking )
{
    if( sink_ )
        sink_->onText( text, thinking );
}

void ChatRequest::onFinish( bool ok, const std::string &reason )
{
    if( reported_ )
        return;                       // `[DONE]` already ended it; this is the close

    // The status is asked about first, before whether the body was well framed: a
    // server that says 401 and then ends the connection oddly has still answered,
    // and "the server said 401: bad credentials" is the fact.  Reported the other
    // way round, a refused request arrives as a complaint about chunk sizes and the
    // user has to guess what a chunk size has to do with their API key.
    if( status_ != 0 && ( status_ < 200 || status_ >= 300 ) )
    {
        std::string message = "the server said " + core::toString( status_ );
        const std::string body = briefBody( errorBody_ );
        if( !body.empty() )
            message += ": " + body;
        finish( false, message );
        return;
    }

    if( !ok )
    {
        finish( false, reason );
        return;
    }

    // The connection ended, and at the HTTP layer that is a complete body: no length
    // promised more, no chunk left unterminated.  It is still not a complete *answer*,
    // because in this protocol the answer says so itself with `[DONE]`.  Treating the
    // close as enough was measured against a server that answered a few characters and
    // hung up: the transcript showed the fragment with nothing after it, which is
    // indistinguishable from an answer that ended there -- and the transcript is what
    // the next request is built from, so the model is then told it said something it
    // never finished saying.  So the close is reported for what it is -- an answer that
    // stopped early -- with the fragment still on screen, because it was said.
    if( !sse_.done() )
    {
        finish( false, "the answer stopped before it was finished" );
        return;
    }

    finish( true, std::string() );
}

void ChatRequest::finish( bool ok, const std::string &why )
{
    if( reported_ )
        return;
    reported_ = true;
    ok_ = ok;
    error_ = why;
    if( sink_ )
        sink_->onFinished( ok, why );
}

std::string ChatRequest::briefBody( const std::string &body )
{
    // Flattened and cut: this string ends up in one line of a transcript, where a
    // JSON body across six lines would push the reason off the screen.
    std::string out;
    for( std::size_t i = 0; i < body.size(); ++i )
    {
        const char c = body[i];
        if( c == '\r' || c == '\n' || c == '\t' )
        {
            if( !out.empty() && out[out.size() - 1] != ' ' )
                out += ' ';
            continue;
        }
        out += c;
        if( out.size() >= kQuotedError )
            break;
    }
    while( !out.empty() && out[out.size() - 1] == ' ' )
        out.erase( out.size() - 1 );
    if( out.size() >= kQuotedError && body.size() > kQuotedError )
        out += "...";
    return out;
}

} // namespace atui::net
} // namespace atui
