// Definitions for the callers with no event loop.  See http_simple.h.
#include "http_simple.h"

#include "http.h"
#include "url.h"

#include "core/strutil.h"

namespace atui {
namespace net {

namespace {

// How much of an error body is kept, and how much of that is quoted back.  A server
// that is complaining usually says so in a sentence; a server streaming an error page
// is not worth holding on to.
const std::size_t kMaxErrorBody = 4096;
const std::size_t kQuotedError = 200;

// Collects one response.
class GetCollector : public HttpStream::Sink
{
public:
    GetCollector() : status_( 0 ), finished_( false ), ok_( false ), bodyLen_( 0 ) {}

    virtual void onStatus( int code ) { status_ = code; }

    virtual void onBody( const char *data, std::size_t len )
    {
        // A body that is going to be reported as an error is only worth a screenful;
        // a body that is the answer (the model list) is small by construction.
        if( failed() )
        {
            if( body_.size() < kMaxErrorBody )
            {
                const std::size_t room = kMaxErrorBody - body_.size();
                body_.append( data, len < room ? len : room );
            }
            return;
        }
        body_.append( data, len );
        bodyLen_ += len;
    }

    virtual void onFinish( bool ok, const std::string &reason )
    {
        finished_ = true;
        ok_ = ok;
        why_ = reason;
    }

    bool finished() const { return finished_; }
    int status() const { return status_; }

    // The message for whoever called: the status leads, because a server that refused
    // and then closed oddly has still refused, and a complaint about chunk sizes does
    // not tell anybody their API key was wrong.
    std::string message() const
    {
        if( status_ != 0 && ( status_ < 200 || status_ >= 300 ) )
        {
            std::string text = "the server said " + core::toString( status_ );
            const std::string brief = briefForMessage( body_ );
            if( !brief.empty() )
                text += ": " + brief;
            return text;
        }
        return why_;
    }

    const std::string &body() const { return body_; }

private:
    bool failed() const
    {
        return status_ != 0 && ( status_ < 200 || status_ >= 300 );
    }

    int status_;
    bool finished_;
    bool ok_;
    std::string why_;
    std::string body_;
    std::size_t bodyLen_;
};

} // anonymous namespace

std::string briefForMessage( const std::string &text )
{
    std::string out;
    for( std::size_t i = 0; i < text.size(); ++i )
    {
        const char c = text[i];
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
    if( out.size() >= kQuotedError && text.size() > kQuotedError )
        out += "...";
    return out;
}

bool httpGet( const std::string &baseUrl,
              const std::string &path,
              const std::string &apiKey,
              int seconds,
              std::string &body,
              std::string &error )
{
    body.clear();
    error.clear();

    HttpEndpoint endpoint;
    if( !parseHttpUrl( baseUrl, endpoint, error ) )
        return false;

    HttpStream stream;
    GetCollector sink;
    if( !stream.start( "GET", endpoint.host, endpoint.port,
                       joinPath( endpoint.path, path ),
                       std::string(), std::string(), std::string(),
                       apiKey.empty() ? std::string() : "Bearer " + apiKey,
                       10, seconds > 0 ? seconds : 20, &sink, &error ) )
    {
        return false;
    }

    // Blocking on purpose: see the header.  The deadline is the transport's, so a
    // silent server is given up on by the same rule the chat path uses.
    pumpUntilDone( stream, seconds > 0 ? seconds : 20 );
    if( stream.busy() )
    {
        stream.abort( "gave up waiting" );
        error = "the server did not answer";
        return false;
    }
    if( !sink.message().empty() )
    {
        error = sink.message();
        return false;
    }
    body = sink.body();
    return true;
}

} // namespace atui::net
} // namespace atui
