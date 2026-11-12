// Implementation of the session store. See tui/sessions.h.
#include "tui/sessions.h"

#include "core/strutil.h"

namespace atui {
namespace tui {

const std::size_t SessionStore::kNoSeat = (std::size_t) -1;

namespace {

// The title a session carries until its first message earns a better
// one, or the user writes one.
std::string autoTitleFor( unsigned long n )
{
    return "Session " + atui::core::toString( n );
}

// Trim of the surrounding blanks; blank-inside stays, because titles
// are sentences sometimes.
std::string trim_( const std::string &s )
{
    std::size_t a = 0, b = s.size();
    while( a != b && ( s[ a ] == ' ' || s[ a ] == '\t' ) )
        ++a;
    while( b != a && ( s[ b - 1 ] == ' ' || s[ b - 1 ] == '\t' ) )
        --b;
    return s.substr( a, b - a );
}

} // anonymous namespace

SessionStore::SessionStore()
    : active_( 0 ), next_( 1 )
{
    add( std::string() );
}

std::size_t SessionStore::add( const std::string &title )
{
    if( seats_.size() >= (std::size_t) kMaxSessions )
        return kNoSeat;

    Session s;
    const std::string given = trim_( title );
    s.title = s.autoTitle = given.empty()
        ? autoTitleFor( next_++ )
        : given;
    seats_.push_back( s );
    return seats_.size() - 1;
}

bool SessionStore::rename( std::size_t i, const std::string &title )
{
    if( i >= seats_.size() )
        return false;
    const std::string given = trim_( title );
    if( given.empty() )
        return false;
    seats_[ i ].title = given;
    return true;
}

bool SessionStore::remove( std::size_t i )
{
    if( i >= seats_.size() )
        return false;
    seats_.erase( seats_.begin() + i );

    if( seats_.empty() )
    {
        // Never empty: the seat resets to a fresh auto-titled session.
        add( std::string() );
        active_ = 0;
        return true;
    }

    // Erasing before the active seat shifts it down by one; erasing at
    // it leaves the seat on what used to be the next session; erasing
    // the last row of all lands it back on the new last.
    if( i < active_ )
        --active_;
    else if( active_ >= seats_.size() )
        active_ = seats_.size() - 1;
    return true;
}

bool SessionStore::setActive( std::size_t i )
{
    if( i >= seats_.size() )
        return false;
    active_ = i;
    return true;
}

} // namespace atui::tui
} // namespace atui
