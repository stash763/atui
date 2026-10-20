// Server-Sent Events: a byte stream, and the payloads inside it.
//
// This layer knows nothing about chat completions.  It turns the bytes of an SSE
// response into complete `data:` payloads and hands them over in order, because
// that is the part with the awkward boundaries in it: a payload arrives split across
// two reads, three reads, or one byte at a time -- measured on the target against a
// real llama.cpp endpoint, where one answer came in 18 pieces of a few hundred bytes
// each and no piece respected a line boundary -- and a client that parses "one read,
// one event" works in a test and breaks on the network.
//
// What the format actually is, as far as these servers use it: lines, ending CRLF or
// LF, and only `data:` lines carry payload.  `event:`, `id:` and `:`-comment lines
// are ignored rather than rejected -- tolerant in the direction that costs nothing,
// since a proxy that adds a comment is not asking to be refused.  A payload of
// `[DONE]` is the stream saying it has finished.
//
// C++98, no threads, no callbacks into anything that blocks: `feed()` is called from
// the event loop with whatever the socket gave up.
#ifndef ATUI_NET_SSE_H
#define ATUI_NET_SSE_H

#include <string>
#include <cstddef>

namespace atui {
namespace net {

class SseStream
{
public:
    class Sink
    {
    public:
        virtual ~Sink() {}
        // One complete payload, with `data:` and its single optional space removed,
        // and the line ending gone.  Called once per `data:` line, in order.
        virtual void onPayload( const std::string &payload ) = 0;
        // `[DONE]` arrived.  Called at most once, after the last onPayload.
        virtual void onDone() = 0;
    };

    SseStream() : done_( false ), overflow_( false ) {}

    // Consumes `len` bytes, emitting whole payloads as they become complete.
    void feed( const char *data, std::size_t len, Sink &sink );

    // True once `[DONE]` has been seen; further feeds are ignored, because a server
    // that keeps talking after the end of the answer is not worth parsing.
    bool done() const { return done_; }

    // A single line longer than `kMaxLine` is not a line: it is a server that has
    // stopped framing, or a port answered by something that is not SSE at all.  The
    // stream stops accepting bytes and says so here, which the caller reports as a
    // broken response rather than growing the buffer forever.
    bool overflowed() const { return overflow_; }

    // Bytes held and not yet forming a line (a test's convenience, and a useful
    // thing to print when an answer stops early).
    std::size_t pending() const { return buf_.size(); }

private:
    // Generous: a tool call's arguments in one payload can be a long JSON object,
    // and an image-bearing message from a client that sends them can be longer,
    // while a stream that produces a 4 MB line is not producing events.
    enum { kMaxLine = 4u * 1024u * 1024u };

    void emit( const std::string &line, Sink &sink );

    std::string buf_;
    bool done_;
    bool overflow_;
};

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_SSE_H
