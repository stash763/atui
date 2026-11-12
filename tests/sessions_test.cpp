// Tests for the session store: the bookkeeping a switch must get right
// (titles, the active seat, the cap, and never empty), plus the title
// cut that names a session from its first message.
#include "harness.h"
#include "core/strutil.h"
#include "tui/sessions.h"

namespace atui_test {

namespace {

// --- the store -----------------------------------------------------------

int test_store_starts_with_one()
{
    atui::tui::SessionStore s;
    if( s.count() != 1 )
        return -1;
    if( s.activeIndex() != 0 )
        return -2;
    if( s.active().title != "Session 1" )
        return -3;
    if( s.active().turn != 0 )
        return -4;          // the store never invents turns
    return 0;
}

int test_store_add_titles()
{
    atui::tui::SessionStore s;
    const std::size_t a = s.add( "" );
    if( a != 1 || s.session( a ).title != "Session 2" )
        return -1;
    const std::size_t b = s.add( "  fix the build  " );
    if( b != 2 || s.session( b ).title != "fix the build" )
        return -2;
    // Adding is not visiting.
    if( s.activeIndex() != 0 )
        return -3;
    // Auto titles never repeat, so a deleted number is not reused
    // next to the sessions that remember it.
    s.remove( a );
    const std::size_t c = s.add( "" );
    if( s.session( c ).title != "Session 3" )
        return -4;
    return 0;
}

int test_store_rename()
{
    atui::tui::SessionStore s;
    if( !s.rename( 0, "  named  " ) )
        return -1;
    if( s.session( 0 ).title != "named" )
        return -2;
    if( s.rename( 0, "   " ) )            // blank is no title
        return -3;
    if( s.session( 0 ).title != "named" )
        return -4;
    if( s.rename( 9, "x" ) )              // no such row
        return -5;
    return 0;
}

int test_store_remove_moves_the_seat()
{
    atui::tui::SessionStore s;         // seats: Session 1
    s.add( "a" );                      // 1
    s.add( "b" );                      // 2
    s.add( "c" );                      // 3
    s.setActive( 2 );                  // on "b"

    s.remove( 1 );                     // erased before the seat
    if( s.count() != 3 || s.activeIndex() != 1 )
        return -1;
    if( s.active().title != "b" )
        return -2;

    s.remove( 1 );                     // the seat itself: the next row up
    if( s.count() != 2 || s.activeIndex() != 1 )
        return -3;
    if( s.active().title != "c" )
        return -4;

    s.remove( 1 );                     // the last row of all: back one seat
    if( s.count() != 1 || s.activeIndex() != 0 )
        return -5;
    return 0;
}

int test_store_never_empty()
{
    atui::tui::SessionStore s;
    if( !s.remove( 0 ) )                // the only session
        return -1;
    if( s.count() != 1 )
        return -2;                      // reset, not emptied
    if( s.active().title == "Session 1" )
        return -3;                      // a fresh number, not the old seat
    if( !s.active().blocks.empty() )
        return -4;
    if( s.active().turn != 0 )
        return -5;
    if( s.remove( 5 ) )                 // no such row
        return -6;
    return 0;
}

int test_store_cap()
{
    atui::tui::SessionStore s;
    for( int i = 0; i < atui::tui::SessionStore::kMaxSessions - 1; ++i )
        if( s.add( "" ) == atui::tui::SessionStore::kNoSeat )
            return -1;
    if( s.count() != (std::size_t) atui::tui::SessionStore::kMaxSessions )
        return -2;
    if( s.add( "one too many" ) != atui::tui::SessionStore::kNoSeat )
        return -3;
    if( s.count() != (std::size_t) atui::tui::SessionStore::kMaxSessions )
        return -4;
    return 0;
}

// --- the title cut -------------------------------------------------------

int test_title_from_text()
{
    // The first line, and only the first.
    if( atui::core::titleFromText( "hello world", 40 ) != "hello world" )
        return -1;
    if( atui::core::titleFromText( "first\nsecond", 40 ) != "first" )
        return -2;
    // Whitespace collapses; the title reads in one row of a list.
    if( atui::core::titleFromText( "   many \t spaces  \n rest", 40 )
        != "many spaces" )
        return -3;
    // Nothing to say: no title, not a row of blanks.
    if( !atui::core::titleFromText( "   \n more", 40 ).empty() )
        return -4;
    if( !atui::core::titleFromText( "", 40 ).empty() )
        return -5;
    // The cut marks itself, and never overruns the width.
    if( atui::core::titleFromText( "0123456789", 5 ) != "0123~" )
        return -6;
    if( atui::core::titleFromText( "01234", 5 ) != "01234" )
        return -7;
    if( atui::core::titleFromText( "cut at a space  x", 15 ) != "cut at a space~" )
        return -8;
    if( !atui::core::titleFromText( "x", 0 ).empty() )
        return -9;
    return 0;
}

} // anonymous namespace

void init_sessions_tests()
{
    register_test( "sessions_starts_with_one", test_store_starts_with_one );
    register_test( "sessions_add_titles", test_store_add_titles );
    register_test( "sessions_rename", test_store_rename );
    register_test( "sessions_remove_moves_the_seat",
                   test_store_remove_moves_the_seat );
    register_test( "sessions_never_empty", test_store_never_empty );
    register_test( "sessions_cap", test_store_cap );
    register_test( "sessions_title_from_text", test_title_from_text );
}

} // namespace atui_test
