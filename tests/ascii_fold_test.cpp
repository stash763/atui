// Tests for the fold to ASCII that stands between the transcript and the screen.
//
// The strings are written with hex escapes, which is the only honest way to test this:
// a UTF-8 source file would let the test's own bytes disagree with the terminal the test
// runs on, and a `\u` escape in a C++ literal is a universal character name that the
// compiler resolves before this function ever sees it.
#include "harness.h"
#include "tui/ascii_fold.h"

#include <cstdlib>
#include <string>

namespace atui_test {

namespace {

using atui::tui::foldToAscii;
using atui::tui::foldForTerminal;
using atui::tui::foldTerminalText;

int test_ascii_passes_through()
{
    if( foldToAscii( "hello, world 42" ) != "hello, world 42" ) return -1;
    if( foldToAscii( "" ) != "" ) return -2;
    // Layout survives: a tool's multi-line output is made of these.
    if( foldToAscii( "one\ntwo\n" ) != "one\ntwo\n" ) return -3;
    if( foldToAscii( "\tindent" ) != "\tindent" ) return -4;
    return 0;
}

int test_typography_folds_to_its_ascii_spelling()
{
    // U+2014 em dash: what the OS/2 box was showing as "ΓÇö".
    if( foldToAscii( "a\xE2\x80\x94" "b" ) != "a-b" ) return -1;
    if( foldToAscii( "\xE2\x80\x93" ) != "-" ) return -2;        // en dash
    if( foldToAscii( "\xE2\x80\x98x\xE2\x80\x99" ) != "'x'" ) return -3;
    if( foldToAscii( "\xE2\x80\x9Cx\xE2\x80\x9D" ) != "\"x\"" ) return -4;
    if( foldToAscii( "wait\xE2\x80\xA6" ) != "wait..." ) return -5;
    if( foldToAscii( "\xE2\x80\xA2 item" ) != "* item" ) return -6;   // bullet
    // Split before the "b": a hex escape keeps taking hex digits, so "\x92b" would be
    // one escape of 0x92B rather than a byte followed by the letter.
    if( foldToAscii( "a\xE2\x86\x92" "b" ) != "a->b" ) return -7;   // right arrow
    if( foldToAscii( "\xE2\x9C\x93" ) != "ok" ) return -8;            // check mark
    return 0;
}

int test_accents_are_kept_as_letters()
{
    // A name should read. "Jos\u00e9 M\u00fcller" in UTF-8.
    if( foldToAscii( "Jos\xC3\xA9" ) != "Jose" ) return -1;
    if( foldToAscii( "M\xC3\xBC""ller" ) != "Muller" ) return -2;
    if( foldToAscii( "\xC3\x9F" ) != "ss" ) return -3;               // sharp s
    if( foldToAscii( "\xC3\x91" ) != "N" ) return -4;                // N-tilde
    return 0;
}

int test_graphics_have_an_ascii_shape()
{
    if( foldToAscii( "\xE2\x94\x82" ) != "|" ) return -1;            // │
    if( foldToAscii( "\xE2\x94\x80" ) != "-" ) return -2;            // ─
    if( foldToAscii( "\xE2\x94\x8C" ) != "+" ) return -3;            // ┌
    if( foldToAscii( "\xE2\x96\x88\xE2\x96\x91" ) != "##" ) return -4;   // █ ░
    if( foldToAscii( "\xE2\xA0\x80" ) != "." ) return -5;            // braille: a spinner
    return 0;
}

int test_what_cannot_be_spelled_is_said_to_be_there()
{
    // U+4F60 and U+1F600: real characters, no ASCII spelling.
    if( foldToAscii( "\xE4\xBD\xA0" ) != "?" ) return -1;
    if( foldToAscii( "\xF0\x9F\x98\x80" ) != "?" ) return -2;
    // A combining mark has nothing to stand on once its base letter is ASCII.
    if( foldToAscii( "e\xCC\x81" ) != "e" ) return -3;
    return 0;
}

int test_control_characters_do_not_reach_the_terminal()
{
    if( foldToAscii( "a\x01\x02""b" ) != "ab" ) return -1;
    if( foldToAscii( "a\x7F""b" ) != "ab" ) return -2;
    // Form feed and vertical tab are control too: a terminal that honours them would
    // move the cursor out from under the view that owns the screen.
    if( foldToAscii( "a\x0C""b" ) != "ab" ) return -3;
    return 0;
}

int test_bytes_that_are_not_utf8_are_left_alone()
{
    // The rule that keeps an 8-bit keyboard working: what is not a valid UTF-8
    // sequence is not this function's business. CP850 'é' is 0x8A, a continuation byte.
    const std::string cp850( "\x8A\xA2\x9A" );
    if( foldToAscii( cp850 ) != cp850 ) return -1;
    // A lead byte whose continuation never arrives stays as it was sent.
    const std::string bad( "\xC3z" );
    if( foldToAscii( bad ) != bad ) return -2;
    // So does a sequence cut off by the end of the string.
    const std::string cut( "x\xE2\x80" );
    if( foldToAscii( cut ) != cut ) return -3;
    return 0;
}

int test_overlong_encodings_are_not_accepted()
{
    // "\xC0\xAF" decodes to '/' the long way round. Accepting it would let a byte
    // sequence slip past a check that looks at the character.
    const std::string overlong( "\xC0\xAF" );
    if( foldToAscii( overlong ) != overlong ) return -1;
    const std::string three( "\xE0\x80\xAF" );
    if( foldToAscii( three ) != three ) return -2;
    return 0;
}

int test_folding_is_only_done_where_the_terminal_needs_it()
{
    const std::string dash( "a\xE2\x80\x94" "b" );
    if( !foldTerminalText() )
    {
        // This machine's terminal can show it, so the view must not have touched it.
        if( foldForTerminal( dash ) != dash ) return -1;
    }
    else
    {
        if( foldForTerminal( dash ) != "a-b" ) return -2;
    }
    return 0;
}

// setenv/unsetenv, so the override can be tested on the platform the test runs on.
static void setFoldEnv( const char *value )
{
#ifdef _WIN32
    std::string e = std::string( "ATUI_FOLD=" ) + ( value ? value : "" );
    _putenv( e.c_str() );
#else
    if( value ) setenv( "ATUI_FOLD", value, 1 );
    else        unsetenv( "ATUI_FOLD" );
#endif
}

// The override is what lets the fold be looked at on a machine whose terminal does not
// need it -- which is how this module was written and tested.
int test_the_environment_can_insist()
{
    setFoldEnv( "1" );
    const bool on = foldTerminalText();
    setFoldEnv( "0" );
    const bool off = foldTerminalText();
    setFoldEnv( 0 );
    const bool fresh = foldTerminalText();

    if( !on ) { setFoldEnv( 0 ); return -1; }
    if( off ) { setFoldEnv( 0 ); return -2; }
    // With nothing set, the answer is the platform's, and folding follows it.
    const std::string dash( "a\xE2\x80\x94" "b" );
    if( foldForTerminal( dash ) == ( fresh ? "a-b" : dash ) )
        return 0;
    return -3;
}

} // anonymous namespace

void init_ascii_fold_tests();
void init_ascii_fold_tests()
{
    register_test( "fold: ascii passes through", &test_ascii_passes_through );
    register_test( "fold: typography folds", &test_typography_folds_to_its_ascii_spelling );
    register_test( "fold: accents keep letters", &test_accents_are_kept_as_letters );
    register_test( "fold: graphics have a shape", &test_graphics_have_an_ascii_shape );
    register_test( "fold: unknown becomes ?", &test_what_cannot_be_spelled_is_said_to_be_there );
    register_test( "fold: no control characters", &test_control_characters_do_not_reach_the_terminal );
    register_test( "fold: not-utf8 bytes survive", &test_bytes_that_are_not_utf8_are_left_alone );
    register_test( "fold: overlong rejected", &test_overlong_encodings_are_not_accepted );
    register_test( "fold: only when needed", &test_folding_is_only_done_where_the_terminal_needs_it );
    register_test( "fold: the environment can insist", &test_the_environment_can_insist );
}

} // namespace atui_test
