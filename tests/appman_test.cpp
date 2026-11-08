// Tests for the hands seam's testable half: the argument parsers every
// window act runs its handle through, and the promise that a rejected
// argument fails with a sentence rather than a post. None of this needs
// a desktop -- same reason as screen_test.cpp, same payoff: the OS/2
// backend can be exercised nowhere and these guarantees still hold.
#include "harness.h"
#include "platform/appman.h"

#include <string>

namespace atui_test {

namespace {

// --- parseWindowHandle --------------------------------------------------

int test_parse_handle_forms()
{
    unsigned long v = 0;

    // The exact form the windows tool prints.
    if( !atui::platform::parseWindowHandle( "0x80000207", v )
        || v != 0x80000207UL )
        return -1;
    // Upper-case prefix and digits both read.
    if( !atui::platform::parseWindowHandle( "0X8A", v ) || v != 0x8AUL )
        return -2;
    // Plain decimal is a handle too -- HWNDs are just numbers.
    if( !atui::platform::parseWindowHandle( "4096", v ) || v != 4096UL )
        return -3;
    // Surrounding spaces are the model being human.
    if( !atui::platform::parseWindowHandle( "  0x10\t", v )
        || v != 0x10UL )
        return -4;
    return 0;
}

int test_parse_handle_rejects()
{
    unsigned long v = 0xDEAD;
    // Zero is no window.
    if( atui::platform::parseWindowHandle( "0", v ) )
        return -1;
    if( atui::platform::parseWindowHandle( "0x0", v ) )
        return -2;
    // Empty, rubbish, half a hex number, a sign, an interior letter.
    if( atui::platform::parseWindowHandle( "", v ) )
        return -3;
    if( atui::platform::parseWindowHandle( "hwnd_desktop", v ) )
        return -4;
    if( atui::platform::parseWindowHandle( "0x", v ) )
        return -5;
    if( atui::platform::parseWindowHandle( "-1", v ) )
        return -6;
    if( atui::platform::parseWindowHandle( "12x", v ) )
        return -7;
    // Decimal digits with a hex letter inside: "0x" prefixes hex only.
    if( atui::platform::parseWindowHandle( "08g", v ) )
        return -8;
    // And the value must survive unchanged: a rejected overflow is
    // better than a truncated one naming somebody else's window.
    if( atui::platform::parseWindowHandle( "0x1FFFFFFFF", v ) )
        return -9;
    if( v != 0xDEAD )
        return -10;
    return 0;
}

// --- parseMessageWord ---------------------------------------------------

int test_parse_message_word()
{
    unsigned long v = 0;
    // Zero is a legal message number; that is the whole difference.
    if( !atui::platform::parseMessageWord( "0", v ) || v != 0UL )
        return -1;
    if( !atui::platform::parseMessageWord( "0x0111", v )
        || v != 0x111UL )
        return -2;
    if( atui::platform::parseMessageWord( "x", v ) )
        return -3;
    return 0;
}

// --- the acts fail clean ------------------------------------------------

int test_acts_reject_bad_handles_without_side_effects()
{
    // Whatever backend is compiled in, an argument that is not a handle
    // must never reach the machine -- and the answer must say so in
    // words. This holds on OS/2 too: the parse runs before any post.
    std::string err;

    if( atui::platform::appCloseWindow( "me", err ) )
        return -1;
    if( err.empty() )
        return -2;

    err.clear();
    if( atui::platform::appClickWindow( "", err ) )
        return -3;
    if( err.empty() )
        return -4;

    err.clear();
    if( atui::platform::appSetWindowText( "0x0", "hi", err ) )
        return -5;
    if( err.empty() )
        return -6;

    err.clear();
    if( atui::platform::appPostMessage( "0xzz", 1, 2, 3, err ) )
        return -7;
    if( err.empty() )
        return -8;
    return 0;
}

} // anonymous namespace

void init_appman_tests()
{
    register_test( "parse_handle_forms", test_parse_handle_forms );
    register_test( "parse_handle_rejects", test_parse_handle_rejects );
    register_test( "parse_message_word", test_parse_message_word );
    register_test( "acts_reject_bad_handles_without_side_effects",
                   test_acts_reject_bad_handles_without_side_effects );
}

} // namespace atui_test
