// Definitions for the event-loop HTTP client.  See http.h for what this is for and
// which parts of HTTP/1.1 it deliberately leaves out.
#include "http.h"

#include "core/strutil.h"      // toString: Watcom has no std::to_string

#include <stdio.h>             // fprintf, for the request dump below
#include <stdlib.h>            // strtol
#include <string.h>            // strlen

namespace atui {
namespace net {

namespace {

// ATUI_HTTP_DEBUG=1 writes the request that is about to go out to stderr.
//
// The reason this exists is a specific kind of dead end: a server answers 400 to a
// request that the same source produces correctly on another platform, and every
// answer from the source file ("the body is built here, it must be this") is a guess
// about bytes nobody has looked at.  This looks at them, on the machine that sent
// them, which for a program whose whole network path is hand-rolled is worth having
// permanently and not just for the one bug.
//
// The Authorization line is replaced with a note. This output is written to a console
// and into whatever log a person redirects, and a bearer token in a scrollback is a
// token that has been shown to someone.
void dumpRequest( const std::string &req )
{
    const char *on = std::getenv( "ATUI_HTTP_DEBUG" );
    if ( !on || !*on || *on == '0' )
        return;

    // Generous, because the usual way to read this is `atui.exe 2>req.dbg` and then
    // look at the file: a chat request is several kilobytes once the tool list is in
    // it, and a dump that cuts off before the part that broke is not much of a dump.
    const std::size_t kShow = 16000;
    std::string shown;
    std::size_t from = 0;
    while ( from < req.size() )
    {
        const std::size_t eol = req.find( "\r\n", from );
        const std::size_t lineEnd = ( eol == std::string::npos ) ? req.size() : eol;
        const std::string line = req.substr( from, lineEnd - from );

        // strncmp rather than string::compare( pos, len, lit ): that overload is not
        // in Watcom's <string>, which answered E715 "member 'compare' has not been
        // declared" -- a reminder that the standard library this target has is older
        // than the language the code is written in.
        if ( std::strncmp( line.c_str(), "Authorization: ", 15 ) == 0 )
            shown += "Authorization: [set, hidden]\r\n";
        else
            shown += line + "\r\n";

        from = ( eol == std::string::npos ) ? req.size() : eol + 2;
        // The rest, once the headers are done, is the body: print it flat, cut short.
        if ( line.empty() )
        {
            const std::size_t rest = req.size() - from;
            const std::size_t take = rest > kShow ? kShow : rest;
            shown.append( req, from, take );
            if ( rest > take )
                shown += "...";
            break;
        }
    }

    std::fprintf( stderr, "[http request]\n%s\n[/http request]\n", shown.c_str() );
    std::fflush( stderr );
}

// A read this size, so a streamed answer arrives in reasonable pieces without one
// huge allocation on a machine with a megabyte to spare.
const std::size_t kReadSize = 4096;

// More than this sitting unparsed is not a slow server, it is a server that is not
// answering the question: a header block is a few hundred bytes and an SSE line is
// a few hundred more.  Failing here is what keeps a confused endpoint from paging a
// small machine to death.
const std::size_t kMaxBuffered = 8u * 1024u * 1024u;

// Bytes handed over in one `step()`, so that a fast server streaming a long answer
// cannot keep the event loop busy and starve the screen.  It is a lot per tick:
// several hundred tokens' worth.
const std::size_t kMaxPerStep = 128u * 1024u;

char lowerChar( char c )
{
    return ( c >= 'A' && c <= 'Z' ) ? (char) ( c - 'A' + 'a' ) : c;
}

bool sameWord( const std::string &s, const char *word )
{
    std::size_t i = 0;
    for ( ; word[i] != '\0' && i < s.size(); ++i )
        if ( lowerChar( s[i] ) != lowerChar( word[i] ) )
            return false;
    return word[i] == '\0' && i == s.size();
}

// Trim spaces and tabs from both ends.
std::string trim( const std::string &s )
{
    std::size_t b = 0, e = s.size();
    while ( b < e && ( s[b] == ' ' || s[b] == '\t' ) ) ++b;
    while ( e > b && ( s[e - 1] == ' ' || s[e - 1] == '\t' ||
                       s[e - 1] == '\r' || s[e - 1] == '\n' ) ) --e;
    return s.substr( b, e - b );
}

// The value of `name` in a header block, or empty.  A repeated header is not a
// thing any of these servers do, so the first is the answer.
std::string headerField( const std::string &head, const char *name )
{
    std::size_t line = head.find( "\r\n" );
    line = ( line == std::string::npos ) ? head.size() : line + 2;

    while ( line < head.size() )
    {
        std::size_t next = head.find( "\r\n", line );
        if ( next == std::string::npos )
            next = head.size();
        const std::string one = head.substr( line, next - line );
        line = next + 2;

        const std::size_t colon = one.find( ':' );
        if ( colon == std::string::npos )
            continue;
        if ( sameWord( trim( one.substr( 0, colon ) ), name ) )
            return trim( one.substr( colon + 1 ) );
    }
    return std::string();
}

// `Transfer-Encoding` naming chunked, which is how llama.cpp streams.
bool saysChunked( const std::string &head )
{
    const std::string te = headerField( head, "Transfer-Encoding" );
    for ( std::size_t i = 0; i + 6 <= te.size(); ++i )
    {
        bool hit = true;
        for ( std::size_t k = 0; k < 7; ++k )
            if ( lowerChar( te[i + k] ) != lowerChar( "chunked"[k] ) )
                hit = false;
        if ( hit )
            return true;
    }
    return false;
}

// A chunk size line: hex, then optionally `;extension`, then CRLF (already gone).
long parseChunkSize( const std::string &line )
{
    long value = 0;
    int digits = 0;
    for ( std::size_t i = 0; i < line.size(); ++i )
    {
        const char c = line[i];
        int digit;
        if ( c >= '0' && c <= '9' )      digit = c - '0';
        else if ( c >= 'a' && c <= 'f' ) digit = c - 'a' + 10;
        else if ( c >= 'A' && c <= 'F' ) digit = c - 'A' + 10;
        else break;
        value = value * 16 + digit;
        ++digits;
        if ( digits > 8 )
            return -1;              // more than 16 MB in one chunk: not a chunk size
    }
    return digits == 0 ? -1 : value;
}

} // anonymous namespace

HttpStream::HttpStream()
    : state_( stIdle )
    , sock_( kNoSocket )
    , targetPort_( 0 )
    , sink_( 0 )
    , sent_( 0 )
    , status_( 0 )
    , chunked_( false )
    , remaining_( -1 )
    , chunkLeft_( 0 )
    , lastChunk_( false )
    , connectSeconds_( 0 )
    , idleSeconds_( 0 )
    , connectDeadline_( 0 )
    , idleDeadline_( 0 )
{
}

HttpStream::~HttpStream()
{
    // The socket goes; the sink does not hear about it.  A sink that is being
    // destroyed with the request cannot be told the request has been destroyed, and
    // an abandoned request is the caller's decision, not one to be reported back
    // into a half-alive UI.  `abort()` is the way to end one that is still wanted.
    if ( sock_ != kNoSocket )
    {
        sockClose( sock_ );
        sock_ = kNoSocket;
    }
}

bool HttpStream::start( const std::string &method,
                        const std::string &host,
                        int port,
                        const std::string &path,
                        const std::string &contentType,
                        const std::string &body,
                        const std::string &accept,
                        const std::string &authorization,
                        int connectSeconds,
                        int idleSeconds,
                        Sink *sink,
                        std::string *reason )
{
    if ( busy() )
    {
        if ( reason )
            *reason = "a request is already running";
        return false;
    }
    if ( sink == 0 )
    {
        if ( reason )
            *reason = "nowhere to send the answer";
        return false;
    }
    if ( host.empty() )
    {
        if ( reason )
            *reason = "no server set";
        return false;
    }

    std::string stackReason;
    if ( !sockStackReady( &stackReason ) )
    {
        if ( reason )
            *reason = stackReason;
        return false;
    }

    // Reset everything a previous request may have left behind: this object is
    // reused turn after turn, and a leftover `remaining_` would end the next answer
    // as soon as it started.
    request_.clear();
    sent_ = 0;
    buf_.clear();
    status_ = 0;
    chunked_ = false;
    remaining_ = -1;
    chunkLeft_ = 0;
    lastChunk_ = false;
    sink_ = sink;

    request_ += method;
    request_ += " ";
    request_ += ( path.empty() ? "/" : path );
    request_ += " HTTP/1.1\r\n";
    request_ += "Host: ";
    request_ += host;
    if ( port != 80 )
    {
        request_ += ":";
        request_ += core::toString( port );
    }
    request_ += "\r\n";
    request_ += "User-Agent: atui ";
    request_ += ATUI_VERSION;
    request_ += "\r\n";
    if ( !accept.empty() )
    {
        request_ += "Accept: ";
        request_ += accept;
        request_ += "\r\n";
    }
    if ( !authorization.empty() )
    {
        request_ += "Authorization: ";
        request_ += authorization;
        request_ += "\r\n";
    }
    if ( !contentType.empty() )
    {
        request_ += "Content-Type: ";
        request_ += contentType;
        request_ += "\r\n";
    }
    // Always sent, including when it is zero: it is what tells the server the body
    // is finished, and it is what lets this client know the body cannot be longer
    // than it says.
    request_ += "Content-Length: ";
    request_ += core::toString( (unsigned long) body.size() );
    request_ += "\r\n";
    // One request per connection, so the end of the answer is the server closing
    // it.  That is the framing a streamed answer needs when the server would
    // otherwise keep the connection open for a next request that is never coming.
    request_ += "Connection: close\r\n";
    request_ += "\r\n";
    request_ += body;
    dumpRequest( request_ );

    // Not connected here: the socket belongs to the first `step()`, which is the first
    // moment somebody is actually there to drive it.  Everything above this line can
    // fail and does, synchronously and usefully -- a server that is not set, a url that
    // is not a url -- and those are worth an answer before the caller commits.  Whether
    // the port is open is not one of those: it arrives as a record either way.
    targetHost_ = host;
    targetPort_ = port;

    connectSeconds_ = connectSeconds > 0 ? connectSeconds : 30;
    idleSeconds_ = idleSeconds > 0 ? idleSeconds : 0;
    connectDeadline_ = std::time( 0 ) + connectSeconds_;
    idleDeadline_ = idleSeconds_ ? ( std::time( 0 ) + idleSeconds_ ) : 0;
    state_ = stConnect;
    return true;
}

void HttpStream::step( int waitMs )
{
    if ( !busy() )
        return;
    if ( waitMs < 0 )
        waitMs = 0;

    if ( state_ == stConnect )
    {
        std::string why;
        if ( sock_ == kNoSocket )
        {
            // The connect `start()` left for here.  From this point to the first byte of
            // the request there is at most one step's wait, whatever the caller's cadence
            // was between the two, which is the point: see targetHost_.
            sock_ = sockConnectStart( targetHost_, targetPort_, &why );
            if ( sock_ == kNoSocket )
            {
                finish( false, why );
                return;
            }
        }
        const SockWhy ready = sockWait( sock_, false, waitMs, &why );
        if ( ready == srError )
        {
            finish( false, why );
            return;
        }
        if ( ready == srOk )
        {
            // On the stack atui is written for first, a connect that is under way
            // returns -1 with no error recorded, so this is the first moment the
            // handshake can be judged (see sock.h).
            const std::string err = sockError( sock_ );
            if ( !err.empty() )
            {
                finish( false, err );
                return;
            }
            state_ = stSend;
        }
        else if ( timedOut() )
        {
            finish( false, "the server did not answer within "
                           + core::toString( connectSeconds_ ) + " seconds" );
            return;
        }
        else
        {
            return;
        }
    }

    if ( state_ == stSend )
    {
        std::size_t moves = 0;
        while ( sent_ < request_.size() )
        {
            std::size_t written = 0;
            std::string why;
            const SockWhy w = sockSend( sock_, &request_[0] + sent_,
                                        request_.size() - sent_, &written, &why );
            if ( w == srAgain )
                break;                       // the stack is full: wait for writable
            if ( w != srOk )
            {
                finish( false, why );
                return;
            }
            sent_ += written;
            if ( ++moves > 64 )
                break;                       // bounded, as everything here is
        }
        if ( sent_ < request_.size() )
        {
            std::string why;
            sockWait( sock_, false, waitMs, &why );
            if ( timedOut() )
                finish( false, "the server stopped reading the request" );
            return;
        }
        state_ = stHead;
    }

    // Reading.  Consume what is buffered, then read once with the caller's wait,
    // then keep reading while bytes are actually arriving -- a streamed answer comes
    // in pieces of a few hundred bytes, and stopping at the first read would add a
    // tick's delay to every piece of it.
    std::size_t moved = 0;
    for ( ;; )
    {
        if ( !consume() )
            return;
        if ( state_ == stDone )
            return;

        const int ms = ( moved == 0 ) ? waitMs : 0;
        const int r = readOnce( ms );
        if ( r < 0 )
            return;                          // finished, and the sink has been told
        if ( r == 0 )
            return;                          // nothing more right now
        moved += kReadSize;
        if ( moved >= kMaxPerStep )
            return;                          // let the screen have a turn
    }
}

void HttpStream::abort( const std::string &reason )
{
    if ( !busy() )
        return;
    finish( false, reason );
}

void HttpStream::finish( bool ok, const std::string &why )
{
    if ( state_ == stDone )
        return;
    state_ = stDone;
    if ( sock_ != kNoSocket )
    {
        sockClose( sock_ );
        sock_ = kNoSocket;
    }
    Sink *sink = sink_;
    sink_ = 0;
    if ( sink != 0 )
        sink->onFinish( ok, why );
}

int HttpStream::readOnce( int waitMs )
{
    std::string why;

    // With bytes already buffered there is nothing to wait for: the caller parses
    // what is here first, so a response that arrived in one piece does not pay for a
    // select() before every read.
    if ( buf_.empty() && waitMs > 0 )
    {
        const SockWhy ready = sockWait( sock_, true, waitMs, &why );
        if ( ready == srError )
        {
            finish( false, why );
            return -1;
        }
        if ( ready != srOk )
        {
            if ( timedOut() )
            {
                finish( false, "the server stopped answering" );
                return -1;
            }
            return 0;
        }
    }

    char scratch[ kReadSize ];
    std::size_t got = 0;
    const SockWhy w = sockRecv( sock_, scratch, sizeof scratch, &got, &why );
    if ( w == srAgain )
    {
        if ( timedOut() )
        {
            finish( false, "the server stopped answering" );
            return -1;
        }
        return 0;
    }
    if ( w == srClosed )
    {
        // The connection ended.  Whether that is the answer or a broken answer
        // depends on how far the framing got: with no length and no chunks the
        // close *is* the end of the body; anything else and it came too soon.
        if ( state_ == stHead )
            finish( false, "the server closed the connection without answering" );
        else if ( lastChunk_ )
            finish( true, std::string() );
        else if ( !chunked_ && remaining_ < 0 )
            finish( true, std::string() );
        else if ( state_ == stLength && remaining_ == 0 )
            finish( true, std::string() );
        else
            finish( false, "the answer ended before it was complete" );
        return -1;
    }
    if ( w != srOk )
    {
        finish( false, why );
        return -1;
    }

    if ( idleSeconds_ )
        idleDeadline_ = std::time( 0 ) + idleSeconds_;
    if ( got > 0 )
        buf_.append( scratch, got );
    return 1;
}

bool HttpStream::consume()
{
    if ( state_ == stHead )
    {
        const std::size_t end = buf_.find( "\r\n\r\n" );
        if ( end == std::string::npos )
            return bufferIsSane();
        const std::string head = buf_.substr( 0, end );
        buf_.erase( 0, end + 4 );
        if ( !parseHead( head ) )
            return false;
    }

    if ( chunked_ )
    {
        for ( ;; )
        {
            if ( state_ == stChunkSize )
            {
                const std::size_t nl = buf_.find( "\r\n" );
                if ( nl == std::string::npos )
                    return bufferIsSane();
                const long size = parseChunkSize( buf_.substr( 0, nl ) );
                buf_.erase( 0, nl + 2 );
                if ( size < 0 )
                {
                    finish( false, "the server's chunked answer is not well formed" );
                    return false;
                }
                if ( size == 0 )
                {
                    lastChunk_ = true;
                    state_ = stChunkEnd;
                }
                else
                {
                    chunkLeft_ = size;
                    state_ = stChunkBody;
                }
                continue;
            }

            if ( state_ == stChunkBody )
            {
                if ( buf_.empty() )
                    return bufferIsSane();
                std::size_t take = buf_.size();
                if ( (long) take > chunkLeft_ )
                    take = (std::size_t) chunkLeft_;
                if ( sink_ != 0 )
                    sink_->onBody( &buf_[0], take );
                buf_.erase( 0, take );
                chunkLeft_ -= (long) take;
                if ( chunkLeft_ == 0 )
                    state_ = stChunkEnd;
                continue;
            }

            // stChunkEnd: the CRLF that follows a chunk, or the trailer block that
            // follows the last one.
            if ( lastChunk_ )
            {
                const std::size_t nl = buf_.find( "\r\n" );
                if ( nl == std::string::npos )
                    return bufferIsSane();     // and a close here is a good end
                buf_.erase( 0, nl + 2 );
                if ( nl == 0 )
                {
                    finish( true, std::string() );
                    return false;
                }
                continue;                       // that line was a trailer
            }
            if ( buf_.size() < 2 )
                return bufferIsSane();
            buf_.erase( 0, 2 );
            state_ = stChunkSize;
        }
    }

    if ( remaining_ >= 0 )
    {
        std::size_t take = buf_.size();
        if ( (long) take > remaining_ )
            take = (std::size_t) remaining_;
        if ( take > 0 )
        {
            if ( sink_ != 0 )
                sink_->onBody( &buf_[0], take );
            buf_.erase( 0, take );
            remaining_ -= (long) take;
        }
        if ( remaining_ == 0 )
        {
            finish( true, std::string() );
            return false;
        }
        return bufferIsSane();
    }

    // No framing: everything until the close is the body.
    if ( !buf_.empty() )
    {
        if ( sink_ != 0 )
            sink_->onBody( &buf_[0], buf_.size() );
        buf_.clear();
    }
    return true;
}

bool HttpStream::parseHead( const std::string &head )
{
    const std::size_t nl = head.find( "\r\n" );
    const std::string statusLine = ( nl == std::string::npos ) ? head
                                                              : head.substr( 0, nl );
    if ( !core::startsWith( statusLine, "HTTP/" ) )
    {
        finish( false, "the server did not send an HTTP answer" );
        return false;
    }
    const std::size_t sp = statusLine.find( ' ' );
    if ( sp == std::string::npos )
    {
        finish( false, "the server's status line is not one" );
        return false;
    }
    const long code = strtol( statusLine.c_str() + sp + 1, 0, 10 );
    if ( code < 100 || code > 999 )
    {
        finish( false, "the server's status code is not a number" );
        return false;
    }

    status_ = (int) code;
    chunked_ = saysChunked( head );
    const std::string length = headerField( head, "Content-Length" );
    remaining_ = length.empty() ? -1 : strtol( length.c_str(), 0, 10 );
    if ( remaining_ < 0 && !chunked_ )
        remaining_ = -1;            // read to the close
    state_ = chunked_ ? stChunkSize : stLength;

    if ( sink_ != 0 )
        sink_->onStatus( status_ );
    return true;
}

bool HttpStream::bufferIsSane()
{
    if ( buf_.size() > kMaxBuffered )
    {
        finish( false, "the answer is larger than atui can hold" );
        return false;
    }
    return true;
}

bool HttpStream::timedOut()
{
    const std::time_t now = std::time( 0 );
    if ( state_ == stConnect )
        return connectSeconds_ > 0 && now >= connectDeadline_;
    return idleSeconds_ > 0 && idleDeadline_ != 0 && now >= idleDeadline_;
}

} // namespace atui::net
} // namespace atui
