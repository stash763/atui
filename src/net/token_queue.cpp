// Definitions for the thread-safe token queue.
#include "token_queue.h"

namespace atui {
namespace net {

void TokenQueue::push( const Token &item )
{
    std::lock_guard< std::mutex > lock( mtx_ );
    items_.push( item );
}

bool TokenQueue::pop( Token &out )
{
    std::lock_guard< std::mutex > lock( mtx_ );
    if( items_.empty() )
        return false;
    out = items_.front();
    items_.pop();
    return true;
}

} // namespace atui::net
} // namespace atui
