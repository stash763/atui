// Implementation of the run_command tool. See run_command_tool.h.
#include "tools/run_command_tool.h"
#include "core/childenv.h"
#include "tools/json_args.h"
#include "tools/preview.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#if !defined(_WIN32)
#include <sys/wait.h>
#define ATUI_POPEN( cmd, mode )  popen( ( cmd ), ( mode ) )
#define ATUI_PCLOSE( p )         pclose( ( p ) )
#else
#define ATUI_POPEN( cmd, mode )  _popen( ( cmd ), ( mode ) )
#define ATUI_PCLOSE( p )         _pclose( ( p ) )
#endif

// The fork/exec path - the one with a deadline and a process group - is used where
// fork() and poll() exist. MS-DOS has neither, and `cmd /c` gives no way to stop
// what it started, so those platforms keep the plain pipe and the limit that comes
// with it (documented in PERMISSIONS.md, because it is a difference a person can
// notice).
#if defined(__unix__) || defined(__APPLE__)
#define ATUI_SPAWN_WITH_DEADLINE 1
#else
#define ATUI_SPAWN_WITH_DEADLINE 0
#endif

#if ATUI_SPAWN_WITH_DEADLINE
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#endif

namespace atui {
namespace tools {

namespace {

// Bound captured output so a chatty command cannot blow up memory or the
// transcript. Everything past this is discarded (the child is still drained).
const std::size_t kMaxOutput = 200 * 1024;

// Default and ceiling for the timeout. Long enough that a release build of a
// mid-sized project finishes unwatched, short enough that a command waiting on a
// prompt nobody is going to answer is not a frozen session. 0 means no limit.
// `long` throughout: MS-DOS has 16-bit `int`s, and the ceiling is a day.
const long kDefaultCommandTimeout = 300;
const long kMaxCommandTimeout = 24 * 60 * 60;

long envLong( const char *name, long current )
{
    const char *v = std::getenv( name );
    if( !v || !v[0] ) return current;
    char *end = 0;
    const long n = std::strtol( v, &end, 10 );
    return ( end && end != v ) ? n : current;
}

std::string envOrEmpty( const char *name )
{
    const char *v = std::getenv( name );
    return ( v && v[0] ) ? std::string( v ) : std::string();
}

// What came back from running the command.
struct ChildResult
{
    std::string output;
    bool truncated;
    bool started;
    bool timedOut;
    bool signalled;
    int signal;
    int code;

    ChildResult()
        : truncated( false ), started( true ), timedOut( false ),
          signalled( false ), signal( 0 ), code( -1 ) {}
};

void appendCapped( ChildResult &r, const char *data, std::size_t n )
{
    if( n == 0 ) return;
    if( r.output.size() >= kMaxOutput )
    {
        r.truncated = true;
        return;
    }
    const std::size_t room = kMaxOutput - r.output.size();
    if( n <= room ) r.output.append( data, n );
    else { r.output.append( data, room ); r.truncated = true; }
}

#if ATUI_SPAWN_WITH_DEADLINE

// Runs `shellCmd` through /bin/sh and waits for it, with two things a pipe from
// popen() cannot offer: a deadline that actually stops the command, and an
// environment with the credentials left out.
//
// The child gets its own process group, because the thing that hangs is rarely the
// shell - it is a grandchild still holding the pipe, and killing the shell alone
// would leave that behind holding the session open. Its stdin is /dev/null: a
// command reading from a terminal nobody is looking at is the other common way a
// tool call never comes back, and end-of-file is the honest answer to give it.
ChildResult runCaptured( const std::string &shellCmd,
                         const std::string &passList,
                         long timeoutSeconds )
{
    ChildResult r;
    const atui::core::ChildEnvironment env( passList );

    int fds[ 2 ];
    if( ::pipe( fds ) != 0 ) { r.started = false; return r; }

    const pid_t pid = ::fork();
    if( pid < 0 )
    {
        ::close( fds[0] ); ::close( fds[1] );
        r.started = false;
        return r;
    }

    if( pid == 0 )
    {
        ::setpgid( 0, 0 );
        ::dup2( fds[1], STDOUT_FILENO );
        ::dup2( fds[1], STDERR_FILENO );
        ::close( fds[0] ); ::close( fds[1] );
        const int nullIn = ::open( "/dev/null", O_RDONLY );
        if( nullIn >= 0 )
        {
            ::dup2( nullIn, STDIN_FILENO );
            if( nullIn > STDIN_FILENO ) ::close( nullIn );
        }

        // A child of a threaded program keeps its parent thread's reasons for
        // hiding signals, and exec() does not clear them: a handler goes back to
        // its default by itself, but a *blocked* signal stays blocked for the whole
        // life of the command. This is a command the user could have typed, so it
        // gets the mask a shell would give it. What might arrive as a result is a
        // held-back signal whose default action either ignores it (WINCH, CHLD) or
        // ends the command - and a command that ignores the second category is not
        // worth having.
        sigset_t nothing;
        sigemptyset( &nothing );
        ::pthread_sigmask( SIG_SETMASK, &nothing, 0 );

        // The strings are literals and a std::string this process still owns;
        // execve only takes them as non-const because UNIX said so.
        char *args[] = { const_cast<char *>( "sh" ), const_cast<char *>( "-c" ),
                         const_cast<char *>( shellCmd.c_str() ), 0 };
        ::execve( "/bin/sh", args, env.argv() );
        ::_exit( 127 );   // _exit: atui's atexit handlers are not this child's
    }

    ::close( fds[1] );
    // Both parent and child create the group: the parent may have to signal it
    // before the child reaches setpgid(), and only one of the two can win.
    ::setpgid( pid, pid );

    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds( timeoutSeconds );

    char buf[ 4096 ];
    for( ;; )
    {
        int waitMs = -1;    // -1: wait as long as it takes
        if( timeoutSeconds > 0 )
        {
            const long long remaining =
                std::chrono::duration_cast< std::chrono::milliseconds >(
                    deadline - std::chrono::steady_clock::now() ).count();
            if( remaining <= 0 ) { r.timedOut = true; break; }
            waitMs = (int) remaining;
        }

        struct pollfd pfd;
        pfd.fd = fds[0];
        pfd.events = POLLIN;
        pfd.revents = 0;
        const int ready = ::poll( &pfd, 1, waitMs );
        if( ready < 0 )
        {
            if( errno == EINTR ) continue;
            break;                    // the pipe is broken; take what waitpid says
        }
        if( ready == 0 ) { r.timedOut = true; break; }

        const ssize_t n = ::read( fds[0], buf, sizeof buf );
        if( n > 0 ) appendCapped( r, buf, (std::size_t) n );
        else if( n == 0 ) break;      // no more output
        else if( errno != EINTR ) break;
    }

    if( r.timedOut )
    {
        // Negative pid: the whole group, so whatever the command started goes with
        // it. That is the entire reason this path exists.
        ::kill( -pid, SIGKILL );

        // One last look at what the command printed before it was stopped, since
        // that is usually the answer to why. Non-blocking: a process that moved out
        // of the group itself would otherwise hold atui open on precisely the thing
        // this code path exists to escape.
        const long flags = ::fcntl( fds[0], F_GETFL, 0 );
        if( flags >= 0 ) ::fcntl( fds[0], F_SETFL, flags | O_NONBLOCK );
        for( ;; )
        {
            const ssize_t n = ::read( fds[0], buf, sizeof buf );
            if( n <= 0 ) break;     // drained, or nothing else is coming
            appendCapped( r, buf, (std::size_t) n );
        }
        if( flags >= 0 ) ::fcntl( fds[0], F_SETFL, flags );
    }
    ::close( fds[0] );

    int status = 0;
    int waited = 0;
    while( ( waited = ::waitpid( pid, &status, 0 ) ) < 0 && errno == EINTR ) {}
    if( waited > 0 )
    {
        if( WIFSIGNALED( status ) )
        {
            r.signalled = true;
            r.signal = WTERMSIG( status );
        }
        else if( WIFEXITED( status ) )
        {
            r.code = WEXITSTATUS( status );
        }
    }
    return r;
}

#else // !ATUI_SPAWN_WITH_DEADLINE

// The portable fallback: popen() through the shell, with the credential-shaped
// variables removed from this process's environment for as long as the child takes
// to start. No deadline, because nothing here can be told to stop: popen() does not
// hand back the pid it spawned, and a timeout that cannot be enforced is a message
// rather than a behaviour.
ChildResult runCaptured( const std::string &shellCmd,
                         const std::string &passList,
                         long /*timeoutSeconds*/ )
{
    ChildResult r;
    FILE *pipe = 0;
    {
        atui::core::HiddenCredentials hideCredentials( passList );
        pipe = ATUI_POPEN( shellCmd.c_str(), "r" );
        // The guard ends here on purpose. popen() has already spawned the child,
        // which copied the scrubbed environment at that moment, so atui's own
        // variables can go back now rather than staying missing for as long as the
        // command runs.
    }
    if( !pipe ) { r.started = false; return r; }

    char buf[ 4096 ];
    std::size_t n;
    while( ( n = std::fread( buf, 1, sizeof buf, pipe ) ) > 0 )
        appendCapped( r, buf, n );    // drained even past the cap, so the child
                                      // is never blocked on a full pipe
    const int status = ATUI_PCLOSE( pipe );
#if defined(WIFEXITED)
    // OS/2 and friends hand back a wait status; MS-DOS and Windows hand back the
    // exit code itself.
    if( WIFSIGNALED( status ) )
    {
        r.signalled = true;
        r.signal = WTERMSIG( status );
    }
    else if( WIFEXITED( status ) )
    {
        r.code = WEXITSTATUS( status );
    }
    else
        r.code = status;
#else
    r.code = status;
#endif
    return r;
}

#endif // ATUI_SPAWN_WITH_DEADLINE

} // anonymous namespace

long &commandTimeoutSeconds()
{
    static long seconds = clampCommandTimeout(
        envLong( "ATUI_COMMAND_TIMEOUT", kDefaultCommandTimeout ) );
    return seconds;
}

long clampCommandTimeout( long seconds )
{
    if( seconds < 0 ) return 0;    // 0 is a choice: no limit
    if( seconds > kMaxCommandTimeout ) return kMaxCommandTimeout;
    return seconds;
}

std::string &passEnvNames()
{
    static std::string names = envOrEmpty( "ATUI_PASS_ENV" );
    return names;
}

bool commandTimeoutIsEnforced()
{
    return ATUI_SPAWN_WITH_DEADLINE != 0;
}

const char *RunCommandTool::name() const
{
    return "run_command";
}

const char *RunCommandTool::description() const
{
    return "Run a shell command on the user's machine and return its combined "
           "stdout/stderr output and exit code. Use for builds, tests, git, "
           "listing or printing files, etc. The command runs in the app's "
           "working directory through the system shell and with the user's own "
           "permissions. It gets no standard input (reading it gives end-of-file) "
           "and none of the credential-shaped environment variables, and if it is "
           "still running when the time limit expires it is killed along with "
           "everything it started. Very long output is truncated.";
}

const char *RunCommandTool::parametersSchema() const
{
    return "{\"type\":\"object\",\"properties\":{"
           "\"command\":{\"type\":\"string\",\"description\":\"Shell command "
           "to execute (POSIX sh syntax)\"}"
           "},\"required\":[\"command\"]}";
}

std::string RunCommandTool::execute( const std::string &argumentsJson )
{
    if( !looksJsonObject( argumentsJson ) )
        return "run_command: arguments must be a JSON object";

    std::string command;
    jsonStringArg( argumentsJson, "command", command );
    if( command.empty() )
        return "run_command: 'command' is required";

    // Merge stderr into stdout so the model sees errors too. The shell runs the
    // string; on POSIX we redirect the shell's stderr onto its stdout (the pipe)
    // up front so even commands that themselves do `1>&2` are captured; cmd.exe
    // has no `exec`, so append `2>&1` there.
#if !defined(_WIN32)
    const std::string shellCmd = "exec 2>&1; " + command;
#else
    const std::string shellCmd = command + " 2>&1";
#endif

    const long timeoutSeconds = commandTimeoutSeconds();
    const ChildResult result = runCaptured( shellCmd, passEnvNames(),
                                            timeoutSeconds );
    if( !result.started )
        return "run_command: failed to start '" + oneLineClip( command, 80 ) + "'";

    std::ostringstream r;
    if( result.timedOut )
    {
        r << "timed out after " << timeoutSeconds
          << "s and was killed, along with everything it started";
    }
    else if( result.signalled )
    {
        r << "terminated by signal " << result.signal;
    }
    else
    {
        r << "exit code " << result.code;
    }

    if( result.output.empty() )
    {
        r << " (no output)";
    }
    else
    {
        // Trim a single trailing newline for readability.
        std::string out = result.output;
        if( out[out.size() - 1] == '\n' ) out.erase( out.size() - 1 );
        r << "\n\n" << out;
        if( result.truncated )
            r << "\n\n[output truncated at " << kMaxOutput << " bytes]";
    }
    return r.str();
}

PermissionSubject RunCommandTool::permissionSubject(
    const std::string &argumentsJson ) const
{
    std::string command;
    jsonStringArg( argumentsJson, "command", command );
    // The model's own text, not the `exec 2>&1;` wrapper execute() adds: the
    // policy is judging what was asked for.
    return PermissionSubject( psCommand, command );
}

std::string RunCommandTool::approvalSummary( const std::string &argumentsJson ) const
{
    std::string command;
    jsonStringArg( argumentsJson, "command", command );
    if( command.empty() )
        return "Run a shell command (no command given)";
    return "Run command:\n  $ " + oneLineClip( command, 200 );
}

std::string RunCommandTool::callPreview( const std::string &argumentsJson ) const
{
    std::string command;
    jsonStringArg( argumentsJson, "command", command );
    if( command.empty() )
        return "run_command  (no command given)";
    // The transcript shows this after a "$ " marker, one shell command line per
    // line of the preview.
    return previewClip( command, kMaxPreview );
}

RunCommandTool g_runCommandTool;

} // namespace atui::tools
} // namespace atui
