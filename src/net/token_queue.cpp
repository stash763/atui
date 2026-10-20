// Definitions for the thread-safe token queue.
#include "token_queue.h"

namespace atui {
namespace net {

void TokenQueue::push( const Token &item )
{
    std::lock_guard< std::mutex > lock( mtx_ );
    if( head_ == items_.size() )
    {
        // Nothing unread left: start over at the front instead of letting the
        // index walk to the end of a vector nobody reads from any more.
        items_.clear();
        head_ = 0;
    }
    items_.push_back( item );
}

bool TokenQueue::pop( Token &out )
{
    std::lock_guard< std::mutex > lock( mtx_ );
    if( head_ >= items_.size() )
        return false;
    out = items_[head_];
    ++head_;
    return true;
}

} // namespace atui::net
} // namespace atui
