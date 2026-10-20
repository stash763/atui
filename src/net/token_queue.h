// Thread-safe token queue: worker thread pushes generated tokens, the TUI
// event-loop thread polls (non-blocking). C++14, network-only.
#ifndef ATUI_NET_TOKEN_QUEUE_H
#define ATUI_NET_TOKEN_QUEUE_H

#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>

namespace atui {
namespace net {

// What kind of transcript item a token represents, so the UI can render each
// distinctly (answer text, collapsible reasoning, transient status, a tool call
// and the result it produced).
//
// A tool call arrives as a pair: tkTool announces the call (and is shown as soon
// as it is made, so a long-running command is visible while it runs), tkToolResult
// carries the outcome and closes the record. Between them tkToolNote says who
// allowed it and why ("auto: inside the project", "agent: runs the tests"), which
// is the difference between atui having checked and atui having guessed.
enum TokenKind {
    tkContent    = 0,
    tkThinking   = 1,
    tkStatus     = 2,
    tkTool       = 3,
    tkToolResult = 4,
    tkToolNote   = 5
};

struct Token {
    std::string text;
    TokenKind kind;
    Token() : text(), kind( tkContent ) {}
    Token( const std::string &t, TokenKind k = tkContent ) : text( t ), kind( k ) {}
};

class TokenQueue
{
public:
    // A constructor now, for the read index: `head_ = 0` written at the member is
    // C++11 and Watcom is not.
    TokenQueue()
        : items_(), head_( 0 )
    {
    }

    void push( const Token &item );

    // Non-blocking pop. Returns true and fills out when an item is available.
    bool pop( Token &out );

private:
    std::mutex mtx_;
    // A vector with an index into it rather than a std::queue: Watcom's <queue>
    // is an empty file (measured: the header opens and declares nothing), and
    // this queue only ever pushes at the back and takes from the front.  The
    // index is what keeps a long answer from being copied -- items_ is emptied
    // once everything in it has been read (see push()).
    std::vector< Token > items_;
    std::size_t head_;
};

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_TOKEN_QUEUE_H
