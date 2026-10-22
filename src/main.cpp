// atui main entry point.
//
// Dispatches:
//   atui --self-test     run the C++ self-test suite and exit
//   atui --version       print the version and exit
//   atui --judge-check   report what the permission policy would make of some
//                        commands, without running any of them
//   atui                 launch the TUI application
//
// The project root - the tree atui treats as "the project" - is captured before
// anything else can resolve a path, because every containment check compares
// against it. `--root` overrides it, which is mostly useful with --judge-check:
// the same command is a different question in a different tree.
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// isatty and fileno, for the "is there a terminal here" check before the UI is started.
// POSIX spells the header one thing and everything else Open Watcom targets spells it
// another; Win32 wants the underscored function as well (see terminalHere()).
#if defined( _WIN32 )
#  include <io.h>
#elif defined( __unix__ ) || defined( __APPLE__ ) || defined( __linux__ ) || defined( __FreeBSD__ )
#  include <unistd.h>
#else
#  include <io.h>
#endif

#include "core/lines.h"      // readLine: Watcom has no std::getline
#include "core/paths.h"
#include "settings/settings.h"
#include "tools/permissions.h"
#include "tui/app.h"
#ifdef ATUI_SELFTEST
#include "harness.h"
#endif

#ifdef ATUI_HAVE_NETWORK
#include "net/permission_judge.h"
#endif

namespace {

void printUsage()
{
    std::printf(
        "usage: atui [--root DIR]            run the chat UI\n"
        "       atui --ask \"question\"         ask it as soon as the interface is up\n"
#ifdef ATUI_SELFTEST
        "       atui --self-test             run the self-test suite\n"
#endif
        "       atui --version\n"
        "       atui --judge-check [--json] [--root DIR] <command>...\n"
        "       atui --judge-check [--json] -           commands from stdin\n"
        "       atui --judge-check [--json] FILE        corpus: one expectation\n"
        "                                               per line, `run` or `ask`,\n"
        "                                               a tab, then the command;\n"
        "                                               exits 1 if any line\n"
        "                                               disagrees\n"
        "\n"
        "Nothing is executed: an allowing verdict is reported, not run.\n" );
}

std::string jsonEscape( const std::string &s )
{
    std::string out;
    for( std::size_t i = 0; i < s.size(); ++i )
    {
        const char c = s[i];
        if( c == '"' )       out += "\\\"";
        else if( c == '\\' ) out += "\\\\";
        else if( c == '\n' ) out += "\\n";
        else if( c == '\t' ) out += "\\t";
        else if( (unsigned char) c < 32 ) out += '?';
        else out += c;
    }
    return out;
}

// Which model would be consulted, if the facts leave anything to consult about.
// Empty means there is nobody to ask, and `--judge-check` says so up front
// instead of quietly reporting "ask" for everything that needed a judge.
std::string agentLabel()
{
#ifdef ATUI_HAVE_NETWORK
    return atui::net::permissionAgentModel();
#else
    return std::string();
#endif
}

// One line per command: the verdict, the rule that gave it, why, and how long the
// agent took. In corpus mode the expectation is appended, and a mismatch is
// called out on the following line so it is visible in a screenful of output.
void report( const std::string &command, const atui::tools::Permission &p,
             const std::string &agent, long agentMs, bool json,
             const char *expected )
{
    const char *word = ( p.outcome == atui::tools::poRun ) ? "run" : "ask";
    const bool ok = !expected ||
        ( ( p.outcome == atui::tools::poRun ) == ( std::string( expected ) == "run" ) );

    if( json )
    {
        std::printf( "{\"command\":\"%s\",\"decision\":\"%s\",\"rung\":%d,"
                     "\"reason\":\"%s\",\"agent\":\"%s\",\"ms\":%ld",
                     jsonEscape( command ).c_str(), word, p.rung,
                     jsonEscape( p.reason ).c_str(), jsonEscape( agent ).c_str(),
                     agentMs );
        if( expected )
            std::printf( ",\"expected\":\"%s\",\"ok\":%s", expected,
                         ok ? "true" : "false" );
        std::printf( "}\n" );
        return;
    }

    std::printf( "%-4s %-44s rung %-2d  %s", word,
                 command.substr( 0, 44 ).c_str(), p.rung, p.reason.c_str() );
    if( !p.detail.empty() )
        std::printf( " [%s]", p.detail.c_str() );
    if( agentMs > 0 )
        std::printf( " (%ldms)", agentMs );
    if( !ok )
        std::printf( "   MISMATCH (expected %s)", expected );
    std::printf( "\n" );
}

// A corpus line is `run<TAB>command` or `ask<TAB>command`; the format itself is
// shared with the self-test that guards it, in tools::parsePermissionCase.

// The permission question, asked of one command line, exactly as the chat loop
// asks it - facts, then the agent - with the answer printed instead of acted on.
// This is the only place the tool loop's inputs are reproducible without a model
// in the middle, which is what makes the judgement measurable at all.
int judgeCheck( int argc, char **argv )
{
    bool json = false, useStdin = false;
    std::string corpus;
    std::vector< std::string > commands;

    for( int i = 2; i < argc; ++i )
    {
        const std::string a = argv[i];
        if( a == "--help" || a == "-h" )
        {
            printUsage();
            return 0;
        }
        if( a == "--json" )                        json = true;
        else if( a == "-" )                        useStdin = true;
        else if( a == "--root" && i + 1 < argc )
        {
            atui::tools::setProjectRoot( argv[++i] );
            // Same rule as the application: one meaning for a relative path.
            atui::core::setWorkingDirectory( atui::tools::projectRoot() );
        }
        else
        {
            std::ifstream probe( a.c_str() );
            if( probe ) corpus = a;   // an existing file is a corpus
            else        commands.push_back( a );
        }
    }

    if( corpus.empty() && commands.empty() && !useStdin )
    {
        printUsage();
        return 2;
    }

    // Config first: providers, then [permissions]. Without a server there is
    // nobody to ask, and every command the facts cannot settle comes back "ask",
    // which this reports rather than hiding.
    atui::settings::loadAll();
#ifdef ATUI_HAVE_NETWORK
    atui::tools::setPermissionJudge( &atui::net::oaiPermissionJudge() );
#endif
    const std::string agent = agentLabel();

    if( !json )
    {
        std::printf( "project root: %s\n", atui::tools::projectRoot().c_str() );
        std::printf( "mode: %s   agent: %s\n\n",
                     atui::tools::permissionModeName(
                         atui::tools::permissionMode() ),
                     agent.empty() ? "(none: nothing is judged)" : agent.c_str() );
    }

    std::ifstream file;
    if( !corpus.empty() ) file.open( corpus.c_str() );
    if( !corpus.empty() && !file )
    {
        std::fprintf( stderr, "cannot read %s\n", corpus.c_str() );
        return 2;
    }

    int checked = 0, bad = 0, lineNumber = 0;
    for( ;; )
    {
        std::string command, expect;
        bool corpusLine = false;

        if( !commands.empty() )
        {
            command = commands.front();
            commands.erase( commands.begin() );
        }
        else if( useStdin )
        {
            std::string line;
            if( !atui::core::readLine( std::cin, line ) ) break;
            const std::size_t from = line.find_first_not_of( " \t\r\n" );
            if( from == std::string::npos ) continue;
            command = line.substr( from );
        }
        else if( !corpus.empty() )
        {
            std::string line;
            if( !atui::core::readLine( file, line ) ) break;
            ++lineNumber;
            const int kind =
                atui::tools::parsePermissionCase( line, expect, command );
            if( kind == atui::tools::kPermissionCaseSkip ) continue;
            if( kind == atui::tools::kPermissionCaseBad )
            {
                // Counted as a failure: a corpus that stops parsing still exits
                // 0 otherwise, which is how a check silently stops checking.
                std::printf( "%s:%d: not a corpus line: %s\n", corpus.c_str(),
                             lineNumber, line.c_str() );
                ++bad;
                continue;
            }
            corpusLine = true;
        }
        else
        {
            // No queued command, no stdin, no corpus: nothing left to read, so
            // stop. The loop used to fall through to the corpus branch here and
            // call readLine() on `file`, which in this branch was never opened --
            // and `if (!file)` would not have caught it, because a stream that was
            // never opened is not in an error state, it is merely empty. Watcom's
            // streams survive the read and take it out behind the bike sheds at
            // exit: SYS1808, exception 0005, three frames deep in teardown, with
            // the answer printed correctly first. Measured both ways on P4 and
            // written up in TESTING.md.
            break;
        }

        if( command.empty() ) continue;

        const std::string arguments =
            std::string( "{\"command\":\"" ) + jsonEscape( command ) + "\"}" ;

        long agentMs = 0;
        const atui::tools::Permission p = atui::tools::decideWithAgent(
            atui::tools::PermissionSubject( atui::tools::psCommand, command ),
            "run_command", arguments, agent, agentMs );

        report( command, p, agent, agentMs, json,
                corpusLine ? expect.c_str() : 0 );
        ++checked;
        if( corpusLine &&
            ( ( p.outcome == atui::tools::poRun ) != ( expect == "run" ) ) )
            ++bad;
    }

    if( !json )
    {
        if( checked == 0 )      std::printf( "nothing to check\n" );
        else if( bad )          std::printf( "\n%d of %d disagreed\n", bad, checked );
    }
    return bad ? 1 : 0;
}

} // anonymous namespace

namespace {

// The command line.  On stdout for --help, on stderr when something has to be explained
// before refusing.
//
// Short, and only ever things that are true of this build: a mistyped option used to be
// ignored, so the help is the place a person looks after the program did nothing with
// what they typed.  The environment variables atui reads are listed in TESTING.md rather
// than here -- help text that duplicates a document is two things that can disagree.
void usage( std::FILE *out )
{
    std::fprintf( out,
        "atui %s -- a terminal AI chat client, over plain HTTP\n"
        "\n"
        "  atui                    start the chat\n"
        "  atui --ask \"question\"   start the chat and ask this as the first message\n"
        "  atui --root DIR         the project the tools work in: inside it a tool runs\n"
        "                          without asking, outside it nothing does\n"
        "  atui --version          print the version and exit\n"
        "  atui --self-test        run the built-in test suite\n"
        "  atui --judge-check ...  say what the permission policy makes of a command,\n"
        "                          and which rule said so:\n"
        "                            --judge-check \"sudo ls\"\n"
        "                            --judge-check cases.txt   lines of \"run|ask <command>\"\n"
        "                            --judge-check -           commands from stdin\n"
        "                          add --json for machine-readable output\n"
        "  atui --help             this text\n"
        "\n"
        "Plain HTTP only: an https:// server is refused, not downgraded.\n",
        ATUI_VERSION );
}

// An option nobody recognised, or one of the two that need a value arriving without it.
// Before this, both were ignored and atui started the chat -- so `atui --help` on a
// machine printed screen codes into a terminal that was not there and waited for a
// keystroke that never came, which is a hang to debug rather than a missing feature.
int usageError( const char *problem )
{
    std::fprintf( stderr, "atui: %s\n\n", problem );
    usage( stderr );
    return 2;
}

// Is there a terminal here to draw on?  atui is a terminal program, and pointed at a pipe
// it paints escape sequences into it and waits for input: measured, `atui` with stdout in
// a pipe ran until killed, and so did `atui --help` when there was no --help.  The three
// modes above are answered before this and need no terminal, which is what makes them
// usable from a script; the chat is not one of those modes.
bool terminalHere()
{
#if defined( _WIN32 )
    return _isatty( _fileno( stdin ) ) != 0 && _isatty( _fileno( stdout ) ) != 0;
#else
    return isatty( fileno( stdin ) ) != 0 && isatty( fileno( stdout ) ) != 0;
#endif
}

} // anonymous namespace

int main( int argc, char **argv )
{
    // First, because "where is atui installed" is answered from it, and that is
    // where the undo journal goes.
    atui::core::setArgv0( ( argc > 0 ) ? argv[0] : 0 );

    if( argc > 1 )
    {
        if( std::strcmp( argv[1], "--self-test" ) == 0 )
        {
#ifdef ATUI_SELFTEST
            atui_test::init();
            return atui_test::run();
#else
            // The suite is 200 KB of code that a machine counting bytes for a
            // 1.5 MB binary cannot spend, so a release build without it has to
            // say what happened rather than treat a known flag as junk.
            std::fprintf( stderr, "atui: this build has no self-test suite "
                                  "(built with -DATUI_BUILD_TESTS=OFF)\n" );
            return 2;
#endif
        }

        if( std::strcmp( argv[1], "--version" ) == 0 )
        {
            std::printf( "atui %s\n", ATUI_VERSION );
            return 0;
        }

        if( std::strcmp( argv[1], "--judge-check" ) == 0 )
            return judgeCheck( argc, argv );
    }

    // `--root` and `--ask` combine in either order, and a question is allowed to begin
    // with a dash -- which is why these are scanned rather than read from argv[1], and
    // why each one's value is taken before the next token is looked at.  Anything else
    // reaches the end of this loop, and is answered with the usage rather than with a
    // chat window.
    for( int i = 1; i < argc; ++i )
    {
        if( std::strcmp( argv[i], "--root" ) == 0 )
        {
            if( ++i >= argc )
                return usageError( "--root needs a directory" );
            // Captured before the UI starts and before any path is resolved: everything
            // the permission policy says is relative to this directory.
            atui::tools::setProjectRoot( argv[i] );
            // And the working directory follows it. Otherwise a relative path in a tool
            // call means one thing to the policy, which resolves it against the root, and
            // another to the write, which resolves it against the directory atui happens
            // to be sitting in - and a policy that disagrees with the filesystem is not a
            // policy.
            atui::core::setWorkingDirectory( atui::tools::projectRoot() );
            continue;
        }

        if( std::strcmp( argv[i], "--ask" ) == 0 )
        {
            if( ++i >= argc )
                return usageError( "--ask needs a question" );
            // Queued here and asked once the prompt exists.
            atui::tui::setStartupMessage( argv[i] );
            continue;
        }

        if( std::strcmp( argv[i], "--help" ) == 0 ||
            std::strcmp( argv[i], "-h" ) == 0 )
        {
            usage( stdout );
            return 0;
        }

        // The three modes are answered above, before this loop, because each replaces
        // the program: say so rather than calling a real option a made-up one.
        if( std::strcmp( argv[i], "--self-test" ) == 0 ||
            std::strcmp( argv[i], "--version" ) == 0 ||
            std::strcmp( argv[i], "--judge-check" ) == 0 )
        {
            std::string why( "'" );
            why += argv[i];
            why += "' is a mode of its own and has to be the first argument";
            return usageError( why.c_str() );
        }

        std::string why( "'" );
        why += argv[i];
        why += "' is not an option atui takes";
        return usageError( why.c_str() );
    }

    if( !terminalHere() )
    {
        std::fprintf( stderr,
            "atui draws on a terminal, and there is not one here: it needs a keyboard on\n"
            "standard input, so it is not starting and sitting there waiting for a key.\n"
            "--version, --self-test and --judge-check work without a terminal; --help\n"
            "lists them.\n" );
        return 2;
    }

    return atui::tui::run_app();
}
