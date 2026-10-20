// HTTP/1.1 as a state machine, so a request can be made from inside the event loop.
//
// This is the transport atui uses where there are no threads to spare -- the Open
// Watcom/OS/2 build in particular, where the screen is drawn by the same execution
// stream that would have to wait for a server.  `step()` is what the event loop
// calls when it is idle: it waits for at most the time it is given, moves whatever
// bytes the stack will move, hands them to the sink, and returns.  Between calls
// nothing runs, and the program stays responsive because it was never inside a
// blocking call to begin with.
//
// What it does understand is the part of HTTP/1.1 a client actually has to:
//
//   * a status line and headers ending at a blank line, which may arrive split at
//     any byte at all (and does: the first `recv()` of a real answer from llama.cpp
//     was 172 bytes of a response whose headers were not finished);
//   * a body framed three ways, because real servers use all three --
//     `Content-Length`, `Transfer-Encoding: chunked` (which is what llama.cpp
//     streams with, measured), and neither, in which case the body is everything
//     until the connection closes;
//   * a response that ends because the server died, which is an error and not an
//     empty answer, and is the difference between "the model said nothing" and "the
//     model never finished saying".
//
// What it does not do: redirects (a LAN endpoint does not redirect, and following
// a `Location:` means a second connection in a state machine that has one), HTTPS
// (the endpoint is on the same LAN, and the whole reason there is a hand-written
// client is that there is no TLS library that will compile here), keep-alive and
// connection pooling (one request per chat turn; the request says `Connection:
// close`, so the server's own last byte is the end of the answer).
//
// Timeouts are whole seconds, taken from the clock when a deadline is set and
// compared again in `step()`.  `select()` provides the waiting; the clock is only
// consulted to notice that waiting has gone on too long, which is a thing measured
// in seconds when a model takes seconds to answer.

#ifndef ATUI_NET_HTTP_H
#define ATUI_NET_HTTP_H

#include "sock.h"

#include <string>
#include <cstddef>
#include <ctime>

namespace atui {
namespace net {

// One request in flight.  Not copyable, and one at a time per instance: the state
// is the request.
class HttpStream
{
public:
    // Where the answer goes.  Each method is called from inside `step()`, which is
    // inside the event loop, so it may draw and must not block.
    class Sink
    {
    public:
        virtual ~Sink() {}
        // The status line, as soon as the headers are complete: 200, 404, 415.
        // Called once, before any body byte.
        virtual void onStatus( int code ) = 0;
        // Body bytes, decoded from any chunked framing, in the order they came.
        // An SSE parser sits on top of this and assembles lines from it.
        virtual void onBody( const char *data, std::size_t len ) = 0;
        // Exactly once per request.  `ok` says the response was received complete;
        // when it is false, `reason` is for a person and says what went wrong.
        virtual void onFinish( bool ok, const std::string &reason ) = 0;
    };

    HttpStream();
    ~HttpStream();            // ends the request if it is still running

    // Sends the request and starts reading the answer.  Returns false only if the
    // request could not be started at all -- no address, no socket, no memory --
    // in which case the sink has not been called and `reason` says why.  A server
    // that refuses, answers late or answers badly is reported through the sink.
    //
    // `contentType` and `accept` are sent when non-empty; `authorization`, when
    // non-empty, goes out as `Authorization: <value>` verbatim (the caller spells
    // the scheme, because `Bearer` is an OpenAI convention and not part of HTTP).
    //
    // `connectSeconds` bounds the handshake; `idleSeconds` bounds the gap between
    // bytes once the answer has started, and 0 means no bound (a model that thinks
    // for minutes holds the connection open without sending anything, and a
    // request timeout the user set is the thing that should give up on it).
    bool start( const std::string &method,
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
                std::string *reason );

    // Advances the request: at most one wait, and then whatever bytes are there.
    // `waitMs` of 0 means "look and return", which is what an idle handler in a TUI
    // wants; a few hundred milliseconds is fine too, and keeps the process quiet
    // while nothing is happening.
    void step( int waitMs );

    // True from the moment `start()` succeeds until `onFinish()` has been called.
    bool busy() const { return state_ != stDone && state_ != stIdle; }

    // Ends the request early (the user pressed escape).  The sink hears about it.
    void abort( const std::string &reason );

    // The request bytes, for a test that wants to assert what went on the wire.
    const std::string &rawRequest() const { return request_; }

private:
    enum State
    {
        stIdle,        // constructed, or never started
        stConnect,     // handshake under way; writable means find out how it went
        stSend,        // writing the request
        stHead,        // reading to the blank line
        stLength,      // body with a known length
        stChunkSize,   // the hex size line of a chunked body
        stChunkBody,   // chunk bytes
        stChunkEnd,    // the CRLF after a chunk
        stDone         // finished: good or bad, the sink has been told
    };

    void finish( bool ok, const std::string &reason );
    // Consumes whatever is in `buf_`, delivering body bytes as it goes.  Returns
    // false when the request has finished (well or badly) and the caller must stop
    // without waiting again.
    bool consume();
    // The status line and the headers: the code, the framing, and `onStatus`.
    bool parseHead( const std::string &head );
    // One read attempt.  Returns 1 when bytes were taken, 0 when there was nothing
    // to take, and -1 when the request is over -- which includes the connection
    // closing, whether that was the end of the answer or too early for it.
    int readOnce( int waitMs );
    // An answer this big is not an answer.
    bool bufferIsSane();
    bool timedOut();

    State state_;
    SockHandle sock_;
    Sink *sink_;

    std::string request_;      // the whole request, for retry-free partial writes
    std::size_t sent_;         // how much of it the stack has taken
    std::string buf_;          // response bytes not yet consumed

    int status_;
    bool chunked_;
    long remaining_;           // bytes of a Content-Length body left; -1: unknown
    long chunkLeft_;           // bytes of the current chunk left
    bool lastChunk_;           // the zero-size chunk has been seen: trailers next

    int connectSeconds_;
    int idleSeconds_;
    std::time_t connectDeadline_;
    std::time_t idleDeadline_;

    // No copying: the socket would be closed twice.
    HttpStream( const HttpStream & );
    HttpStream &operator=( const HttpStream & );
};

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_HTTP_H
