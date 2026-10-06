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

class TokenQueue
{
public:
    void push( const std::string &item );

    // Non-blocking pop. Returns true and fills out when an item is available.
    bool pop( std::string &out );

private:
    std::mutex mtx_;
    std::queue< std::string > items_;
};

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_TOKEN_QUEUE_H
