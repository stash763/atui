// Tests for the prompt file's pure parts (the quoting and the fit)
// and its file parts (ensure and load), plus one honest look at the
// editor default. No shell is spawned and no editor is started: the
// spawn is one system() call whose arguments these tests pin down.
#include "harness.h"
#include "core/hostenv.h"
#include "core/promptfile.h"

#include <cstdio>
#include <string>

namespace atui_test {

namespace {

const char *kTmpPrompts = "atui_promptfile_test.txt";

// --- quoting ---------------------------------------------------------

int test_quote_posix()
{
    if( atui::core::quotePosixArg( "plain" ) != "'plain'" )
        return -1;
    if( atui::core::quotePosixArg( "two words" ) != "'two words'" )
        return -2;
    // The quote inside: close, escaped quote, reopen.
    if( atui::core::quotePosixArg( "it's" ) != "'it'\\''s'" )
        return -3;
    // Empty still quotes: an empty path is an argument, not nothing.
    if( atui::core::quotePosixArg( "" ) != "''" )
        return -4;
    return 0;
}

int test_quote_cmd()
{
    // No spaces, no wrapping; the shell does not need the noise.
    if( atui::core::quoteCmdArg( "C:\\OS2\\E.EXE" ) != "C:\\OS2\\E.EXE" )
        return -1;
    if( atui::core::quoteCmdArg( "C:\\My Docs\\p.txt" )
        != "\"C:\\My Docs\\p.txt\"" )
        return -2;
    if( atui::core::quoteCmdArg( "\"already\"" ) != "\"already\"" )
        return -3;
    if( atui::core::quoteCmdArg( "" ) != "\"\"" )
        return -4;
    return 0;
}

// --- preview fitting ---------------------------------------------------

int test_fit_preview()
{
    if( atui::core::fitPreviewLine( "hello", 10 ) != "hello" )
        return -1;
    if( atui::core::fitPreviewLine( "hello", 5 ) != "hello" )
        return -2;
    if( atui::core::fitPreviewLine( "hello", 3 ) != "he~" )
        return -3;
    if( atui::core::fitPreviewLine( "hello", 1 ) != "~" )
        return -4;
    if( !atui::core::fitPreviewLine( "", 5 ).empty() )
        return -5;
    return 0;
}

// --- the file ----------------------------------------------------------

int test_ensure_creates_and_spares()
{
    std::string err;
    std::remove( kTmpPrompts );
    if( !atui::core::ensurePromptsFile( kTmpPrompts, err ) )
        return -1;
    std::FILE *f = std::fopen( kTmpPrompts, "rb" );
    if( f == 0 )
        return -2;
    std::fclose( f );

    // An existing file survives the call untouched.
    f = std::fopen( kTmpPrompts, "wb" );
    std::fputs( "keep me\n", f );
    std::fclose( f );
    if( !atui::core::ensurePromptsFile( kTmpPrompts, err ) )
        return -3;
    f = std::fopen( kTmpPrompts, "rb" );
    char buf[ 16 ] = { 0 };
    if( f == 0 || std::fgets( buf, sizeof buf, f ) == 0 )
    {
        std::fclose( f );
        return -4;
    }
    std::fclose( f );
    if( std::string( buf ) != "keep me\n" )
        return -5;
    return 0;
}

int test_load_lines()
{
    std::FILE *f = std::fopen( kTmpPrompts, "wb" );
    if( f == 0 )
        return -1;
    // CRLF endings, an interior blank line, a final line without a
    // newline, and no phantom line after the last newline.
    std::fputs( "one\r\ntwo\r\n\r\nthree", f );
    std::fclose( f );

    atui::core::AgentPrompts &p = atui::core::agentPrompts();
    const std::vector<std::string> was = p.extra;

    std::string err;
    if( !atui::core::loadPromptsFile( kTmpPrompts, err ) )
    {
        p.extra = was;
        return -2;
    }
    int rc = 0;
    if( p.extra.size() != 4 )
        rc = -3;
    else if( p.extra[ 0 ] != "one" || p.extra[ 1 ] != "two"
             || !p.extra[ 2 ].empty() || p.extra[ 3 ] != "three" )
        rc = -4;

    p.extra = was;
    std::remove( kTmpPrompts );
    return rc;
}

// --- the editor choice ---------------------------------------------------

int test_editor_never_empty()
{
    // Override, $EDITOR, or the platform default -- some answer
    // always stands, because the Edit button must be able to name
    // what it is about to run.
    if( atui::core::promptsEditor().empty() )
        return -1;
    return 0;
}

} // anonymous namespace

void init_promptfile_tests()
{
    register_test( "promptfile_quote_posix", test_quote_posix );
    register_test( "promptfile_quote_cmd", test_quote_cmd );
    register_test( "promptfile_fit_preview", test_fit_preview );
    register_test( "promptfile_ensure_creates_and_spares",
                   test_ensure_creates_and_spares );
    register_test( "promptfile_load_lines", test_load_lines );
    register_test( "promptfile_editor_never_empty",
                   test_editor_never_empty );
}

} // namespace atui_test
