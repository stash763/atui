// Definitions for the SSE framing.  See sse.h.
#include "sse.h"

#include "core/strutil.h"      // startsWith: Watcom has no std::string::starts_with

namespace atui {
namespace net {

void SseStream::feed( const char *data, std::size_t len, Sink &sink )
{
    if ( done_ || overflow_ )
        return;

    buf_.append( data, len );

    for ( ;; )
    {
        const std::size_t nl = buf_.find( '\n' );
        if ( nl == std::string::npos )
            break;                       // no complete line yet: wait for more
        std::string line = buf_.substr( 0, nl );
        buf_.erase( 0, nl + 1 );
        if ( !line.empty() && line[line.size() - 1] == '\r' )
            line.erase( line.size() - 1 );       // CRLF, and no back() on Watcom
        emit( line, sink );
        if ( done_ )
            return;
    }

    if ( buf_.size() > kMaxLine )
    {
        buf_.clear();
        overflow_ = true;
    }
}

void SseStream::emit( const std::string &line, Sink &sink )
{
    // `data:` with no space is legal and `data: ` with one is what these servers
    // send; more than one space is part of the payload, which is why only one is
    // removed.
    if ( !core::startsWith( line, "data:" ) )
        return;

    std::size_t start = 5;
    if ( start < line.size() && line[start] == ' ' )
        ++start;
    const std::string payload = line.substr( start );

    if ( payload == "[DONE]" )
    {
        done_ = true;
        sink.onDone();
        return;
    }
    if ( payload.empty() )
        return;                 // an empty `data:` line is a keep-alive, not content

    sink.onPayload( payload );
}

} // namespace atui::net
} // namespace atui
