// Thread-safe token queue: worker thread pushes generated tokens, the TUI
// event-loop thread polls (non-blocking). C++14, network-only.
#ifndef ATUI_NET_TOKEN_QUEUE_H
#define ATUI_NET_TOKEN_QUEUE_H

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace atui {
namespace net {

// What kind of transcript item a token represents, so the UI can render each
// distinctly (answer text, collapsible reasoning, transient tool status).
enum TokenKind { tkContent = 0, tkThinking = 1, tkStatus = 2 };

struct Token {
    std::string text;
    TokenKind kind;
    Token() : text(), kind( tkContent ) {}
    Token( const std::string &t, TokenKind k = tkContent ) : text( t ), kind( k ) {}
};

class TokenQueue
{
public:
    void push( const Token &item );

    // Non-blocking pop. Returns true and fills out when an item is available.
    bool pop( Token &out );

private:
    std::mutex mtx_;
    std::queue< Token > items_;
};

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_TOKEN_QUEUE_H
