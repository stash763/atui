// Implementation of the run_command tool. See run_command_tool.h.
#include "tools/run_command_tool.h"
#include "core/childenv.h"
#include "tools/json_args.h"
#include "tools/preview.h"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/strutil.h"
#include "core/int64.h"      // Int64: what a duration's count() answers with

// Three spawn implementations, chosen here and nowhere else:
//
//   fork()/execve()  POSIX.  A deadline, a process group, and a curated
//                    environment handed over explicitly.
//   CreateProcess()  Windows.  The same three things: a job object plays the part
//                    of the process group, and the environment goes as a block.
//   _popen()         MS-DOS and OS/2.  Watcom has no fork, its C library spells
//                    the pipe _popen (measured: <stdio.h> declares _popen and no
//                    unsuffixed popen), and nothing there can be told to stop a
//                    command once started -- so the limit is a message rather
//                    than a behaviour, and `commandTimeoutIsEnforced()` says so
//                    instead of letting the difference be discovered by someone
//                    waiting for a build that finished an hour ago.
//
// Measured on the Windows target before this was written (probe_spawn.cpp, in the
// TESTING.md entry): `_popen` on Windows loses a command's stderr and, worse,
// loses the message cmd.exe prints when a command fails -- `type nosuchfile` came
// back as "exit code 1" with nothing else.  That is what this third arm replaces.
#if defined(__unix__) || defined(__APPLE__)
#define ATUI_SPAWN_FORK_EXEC 1
#include <sys/wait.h>
#elif defined(_WIN32)
#define ATUI_SPAWN_WIN32 1
#else
#define ATUI_POPEN( cmd, mode )  _popen( ( cmd ), ( mode ) )
#define ATUI_PCLOSE( p )         _pclose( ( p ) )
#endif

// Can a command that outlives its limit actually be stopped?  Two of the three
// arms above can; the question is asked by the capability function below, by the
// tool's own description, and by the tests that would otherwise assert a
// behaviour the platform cannot provide.
#if defined( ATUI_SPAWN_FORK_EXEC ) || defined( ATUI_SPAWN_WIN32 )
#define ATUI_SPAWN_HAS_DEADLINE 1
#else
#define ATUI_SPAWN_HAS_DEADLINE 0
#endif

#include "core/signals.h"

#if defined( ATUI_SPAWN_FORK_EXEC )
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#endif

#if defined( ATUI_SPAWN_WIN32 )
// Lean, and without min/max: they are function-like macros that eat identifiers
// in whatever header happens to come next, and there is no reason for this file
// to export them to the rest of atui.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vector>
#endif

namespace atui {
namespace tools {

// Set when a command has finished, taken by the UI the next time it looks.
// Written on whichever thread is stepping the turn and read after the UI has
// popped a record off that turn's queue -- the queue's mutex is what orders the
// two.  What it is for is a child that wrote to the terminal itself: output is
// piped, but a password prompt goes to the controlling tty, which is the screen
// atui is drawing on, and no view has any reason to draw that row again.
static volatile bool g_terminalTouched = false;

namespace {

// Bound captured output so a chatty command cannot blow up memory or the
// transcript. Everything past this is discarded (the child is still drained).
const std::size_t kMaxOutput = 200 * 1024;

// Default and ceiling for the timeout. Long enough that a release build of a
// mid-sized project finishes unwatched, short enough that a command waiting on a
// prompt nobody is going to answer is not a frozen session. 0 means no limit, and
// the range is in the header because the Settings dialog prints it. `long`
// throughout: MS-DOS has 16-bit `int`s, and the ceiling is a day.
const long kDefaultCommandTimeout = 300;

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
    bool stopRequested;   // atui was asked to leave while this was running
    bool signalled;
    int signal;
    int code;

    ChildResult()
        : truncated( false ), started( true ), timedOut( false ),
          stopRequested( false ), signalled( false ), signal( 0 ), code( -1 ) {}
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

#if defined( ATUI_SPAWN_FORK_EXEC )

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

    // How long a stop request is allowed to go unnoticed while a command runs.  The
    // wait below would rather sleep until the deadline, which is a reasonable way to
    // wait for a command and an unreasonable one to wait for the user to change their
    // mind: measured with `sleep 40` running, SIGTERM took 40.2s to produce an exit --
    // the command's own runtime -- because nothing in the path between the signal and
    // this loop could interrupt it.  A fifth of a second is 5 wakeups a second while a
    // tool runs, which is what "prompt" costs here.
    const int kStopCheckMs = 200;

    char buf[ 4096 ];
    for( ;; )
    {
        // Asked to leave: the group goes with us.  This is the same kill the timeout
        // below uses, for the same reason -- the command may have started things of
        // its own, and leaving them running because atui is on its way out is not a
        // decision anybody agreed to.
        if( atui::core::shutdownRequested() )
        {
            r.stopRequested = true;
            break;      // the kill and the drain below are the timeout path's job
        }

        int waitMs = kStopCheckMs;
        if( timeoutSeconds > 0 )
        {
            const core::Int64 remaining =
                std::chrono::duration_cast< std::chrono::milliseconds >(
                    deadline - std::chrono::steady_clock::now() ).count();
            if( remaining <= 0 ) { r.timedOut = true; break; }
            if( remaining < waitMs ) waitMs = (int) remaining;
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
        if( ready == 0 ) continue;    // our own check coming due, not the deadline

        const ssize_t n = ::read( fds[0], buf, sizeof buf );
        if( n > 0 ) appendCapped( r, buf, (std::size_t) n );
        else if( n == 0 ) break;      // no more output
        else if( errno != EINTR ) break;
    }

    if( r.timedOut || r.stopRequested )
    {
        // Negative pid: the whole group, so whatever the command started goes with
        // it. That is the entire reason this path exists, and it is why a stop request
        // ends up here instead of doing its own kill: the drain underneath is what
        // stops a command that left the group on its own from holding atui open while
        // it is trying to leave.
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

#elif defined( ATUI_SPAWN_WIN32 )

// The Windows spawn: CreateProcess, with the three things the fork/exec path has
// and _popen cannot give -- a deadline, a kill that reaches the whole tree, and an
// environment chosen rather than inherited -- plus the merge that this whole file
// exists to get right.
//
// The merge is a handle question, not a syntax question.  `hStdError` is set to the
// same handle as `hStdOutput`, so there is no second stream for a message to escape
// down; measured against the alternative (probe_spawn.cpp, and _popen in
// probe_popen.cpp, both in TESTING.md):
//
//   command                _popen captured      this captured
//   echo boom 1>&2         nothing              boom
//   type nosuchfile.txt    nothing, rc=1        The system cannot find the file
//                                               specified., rc=1
//   nosuchcommand          nothing              'nosuchcommand' is not recognized...
//
// The second row is the one that matters: a command that fails for a reason the
// shell reports used to reach the model as "exit code 1" and nothing else, the
// explanation having gone to the console underneath the TUI.
//
// And appending `2>&1` to the command, which is what this file used to do, is worse
// than doing nothing: cmd.exe applies redirections left to right, so for
// `echo boom 1>&2 2>&1` the second one points stderr at what is already stderr and
// nothing reaches the pipe (measured).
ChildResult runCaptured( const std::string &shellCmd,
                         const std::string &passList,
                         long timeoutSeconds )
{
    ChildResult r;
    const atui::core::ChildEnvironment env( passList );

    // One pipe, both of the child's output handles on the write end.
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof( sa );
    sa.lpSecurityDescriptor = 0;
    sa.bInheritHandle = TRUE;
    HANDLE readEnd = 0, writeEnd = 0;
    if( !::CreatePipe( &readEnd, &writeEnd, &sa, 0 ) ) { r.started = false; return r; }
    // The read end must not be inherited.  It is not only tidiness: a handle the
    // child holds open is a writer's half of a pipe that never ends, and the drain
    // below waits for the last writer to go.
    ::SetHandleInformation( readEnd, HANDLE_FLAG_INHERIT, 0 );

    // stdin is NUL, as on POSIX: a command reading input from a session where the
    // input is a model's decision is the classic way a tool call never comes back,
    // and end-of-file is the honest answer to give it.
    HANDLE nullIn = ::CreateFileA( "NUL", GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0 );

    STARTUPINFOA si;
    ::ZeroMemory( &si, sizeof( si ) );
    si.cb = sizeof( si );
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = ( nullIn != INVALID_HANDLE_VALUE )
                 ? nullIn : ::GetStdHandle( STD_INPUT_HANDLE );
    si.hStdOutput = writeEnd;
    si.hStdError = writeEnd;

    PROCESS_INFORMATION pi;
    ::ZeroMemory( &pi, sizeof( pi ) );

    // CreateProcess does not run a shell, and what the model writes is a shell
    // command line, so cmd.exe it is -- `/s /c "..."` so a command that contains
    // its own quotes still reaches cmd as one unit (measured: a tasklist filter
    // with a quoted argument survives this form).
    const std::string line = "cmd.exe /s /c \"" + shellCmd + "\"";
    std::vector<char> cmdLine( line.begin(), line.end() );
    cmdLine.push_back( '\0' );   // CreateProcess writes to this buffer

    // CreateProcess takes the environment as LPVOID and only reads it; the const
    // belongs to this object, not to the API, so it comes off at the call rather
    // than in the accessor where it would misdescribe what `block()` hands back.
    const BOOL started = ::CreateProcessA(
        0, &cmdLine[0], 0, 0, TRUE,
        CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP,
        const_cast< char * >( env.block() ), 0, &si, &pi );

    // The parent's copy of the write end goes now, whether or not the child came:
    // while we hold it, the pipe has a live writer and every read waits for
    // something that will never arrive.
    ::CloseHandle( writeEnd );

    if( !started )
    {
        ::CloseHandle( readEnd );
        if( nullIn != INVALID_HANDLE_VALUE ) ::CloseHandle( nullIn );
        r.started = false;
        return r;
    }
    r.started = true;

    // The process group of this platform is a job object, and the child is put in
    // it before its first instruction runs -- which is why it was created
    // suspended.  KILL_ON_JOB_CLOSE is what makes "killed along with everything it
    // started" true: a `start /b` grandchild goes with the job (measured, and the
    // self-test asserts it), and closing the handle kills the tree even if atui
    // itself dies, so a command cannot outlive the session that ran it.
    //
    // Assignment can fail -- a box where atui is itself inside a job that forbids
    // nesting.  Then the kill reaches the command alone, which is worse and is not
    // a reason to refuse to run it; the tests report it as a failure rather than a
    // pass, so the difference cannot hide.
#if defined( __BORLANDC__ )
    // No job object on this build, and the reason is the operating system rather
    // than the compiler: the job API arrived with Windows 2000 and BC5's headers
    // do not contain it (measured: "Call to undefined function
    // 'CreateJobObjectA'", with <windows.h> included and without), which matches
    // the systems this build is for -- Windows 98 and NT 4 have no job objects at
    // all.  So the command is the only thing the kill below reaches: a `start /b`
    // grandchild can outlive it, and the tree can outlive atui.  `inJob` is the
    // question the rest of this function already asks -- assignment can fail on a
    // box where atui itself is in a job that forbids nesting -- so the era build
    // takes the fallback path that was written for that, and says so here rather
    // than in a test that would only be able to report it as a failure.
    const HANDLE job = 0;
    const bool inJob = false;
#else
    HANDLE job = ::CreateJobObjectA( 0, 0 );
    bool inJob = false;
    if( job )
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
        ::ZeroMemory( &limits, sizeof( limits ) );
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        inJob = ::SetInformationJobObject( job, JobObjectExtendedLimitInformation,
                                           &limits, sizeof( limits ) ) &&
                ::AssignProcessToJobObject( job, pi.hProcess );
    }
#endif
    ::ResumeThread( pi.hThread );

    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds( timeoutSeconds );

    // The same fifth of a second the POSIX loop wakes on, for the same reason: a
    // stop request should not have to wait out the command to be noticed.  Here it
    // is a poll rather than a poll() timeout because a named pipe has no waitable
    // "data arrived" object without overlapped I/O, and this is not worth the two
    // extra handles and the completion event it would take.  Measured cost of the
    // loop: a command that does nothing returns in 31 ms.
    const DWORD kPollMs = 50;

    bool exited = false;
    char buf[ 4096 ];
    for( ;; )
    {
        if( atui::core::shutdownRequested() )
        {
            // On Windows this is currently always false -- core/signals.cpp has no
            // console control handler -- so the exit that ought to take the tree
            // with it does not go through here.  The job handle is what covers it:
            // when atui's process goes, so do its handles, and KILL_ON_JOB_CLOSE
            // does the rest.  The check stays because it is the right place for the
            // one, whenever signals.cpp grows it.
            r.stopRequested = true;
            break;
        }

        // Drain first, always, and as much as there is: a command that fills the
        // pipe while we were looking for something else is a command that never
        // continues.  Past the cap the bytes are dropped rather than kept, and the
        // reading goes on, which is what stops the child blocking on a full pipe.
        DWORD available = 0, total = 0;
        if( !::PeekNamedPipe( readEnd, 0, 0, 0, &available, &total ) )
            break;                       // broken: every writer has gone
        if( available > 0 )
        {
            DWORD got = 0;
            if( !::ReadFile( readEnd, buf, sizeof buf, &got, 0 ) || got == 0 ) break;
            appendCapped( r, buf, (std::size_t) got );
            continue;
        }

        if( ::WaitForSingleObject( pi.hProcess, 0 ) == WAIT_OBJECT_0 )
            exited = true;

        if( timeoutSeconds > 0 &&
            std::chrono::steady_clock::now() >= deadline ) { r.timedOut = true; break; }
        if( exited ) break;
        ::Sleep( kPollMs );
    }

    if( r.timedOut || r.stopRequested )
    {
#if defined( __BORLANDC__ )
        ::TerminateProcess( pi.hProcess, 1 );      // no job object: see above
#else
        if( inJob ) ::TerminateJobObject( job, 1 );
        else        ::TerminateProcess( pi.hProcess, 1 );
#endif

        // What it printed before it was stopped is usually the answer to why.
        // Bounded, and bounded by data rather than by the clock: the tree is dead,
        // so the pipe has either finished emptying or has a writer outside the job
        // that atui is not going to wait for.
        for( int guard = 0; guard < 64; ++guard )
        {
            DWORD available = 0, total = 0;
            if( !::PeekNamedPipe( readEnd, 0, 0, 0, &available, &total ) ||
                available == 0 )
                break;
            DWORD got = 0;
            if( !::ReadFile( readEnd, buf, sizeof buf, &got, 0 ) || got == 0 ) break;
            appendCapped( r, buf, (std::size_t) got );
        }
    }

    ::WaitForSingleObject( pi.hProcess, 5000 );
    DWORD code = 0;
    if( ::GetExitCodeProcess( pi.hProcess, &code ) )
        r.code = (int) code;
    // No `signalled`: Windows has no wait status, and a crash arrives as its
    // NTSTATUS exit code (0xC0000005 and friends).  That is reported as an exit
    // code, which is what it is here, rather than dressed up as a signal number.

    ::CloseHandle( pi.hThread );
    ::CloseHandle( pi.hProcess );
    if( job ) ::CloseHandle( job );
    ::CloseHandle( readEnd );
    if( nullIn != INVALID_HANDLE_VALUE ) ::CloseHandle( nullIn );
    return r;
}

#else // OS/2, MS-DOS: the pipe that cannot be stopped

// The last fallback, for the two platforms with nothing better: popen() through
// the shell, with the credential-shaped variables removed from this process's
// environment for as long as the child takes to start (which is why
// `HiddenCredentials` exists -- popen copies the parent's environment at the
// moment of the call, so that is the only moment to have removed them).  No
// deadline, because nothing here can be told to stop: popen() does not hand back
// the pid it spawned, and a timeout that cannot be enforced is a message rather
// than a behaviour.
ChildResult runCaptured( const std::string &shellCmd,
                         const std::string &passList,
                         long /*timeoutSeconds*/ )
{
    ChildResult r;
    std::FILE *pipe = 0;   // std::: Watcom's <cstdio> keeps FILE out of the global namespace
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

#endif // spawn implementation

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
    // The capability is the question "can a command that outlives its limit be
    // stopped", and it is answered here, once, rather than by each caller guessing
    // from the platform: OS/2 and MS-DOS say no and say it out loud (the tool
    // description and PERMISSIONS.md both), Windows now says yes.
    return ATUI_SPAWN_HAS_DEADLINE != 0;
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
           "and none of the credential-shaped environment variables. "
#if ATUI_SPAWN_HAS_DEADLINE
           "If it is still running when the time limit expires it is killed "
           "along with everything it started. "
#else
           // The claim above is a capability, and on these platforms it is not
           // one: the shell gives nothing back to stop, so a command that hangs
           // hangs, and the model is told that rather than promised a kill that
           // will not come.  `commandTimeoutIsEnforced()` is the same answer in
           // code; this is it in the sentence the model reads.
           "This host cannot stop a command that has started, so the time limit "
           "ends atui's wait rather than the command: keep each command short. "
#endif
           "Very long output is truncated. "
#if defined( __OS2__ )
           "The shell here is the OS/2 command processor (cmd.exe), NOT a "
           "POSIX sh: use OS/2 commands and backslash paths, as the system "
           "message describes.";
#else
           "The host operating system and its shell are described in the "
           "system message; use that system's idioms.";
#endif
}

const char *RunCommandTool::parametersSchema() const
{
    return "{\"type\":\"object\",\"properties\":{"
           "\"command\":{\"type\":\"string\",\"description\":\"Shell "
#if defined( __OS2__ )
           "command in OS/2 cmd.exe syntax (DIR, COPY, TYPE); not sh\""
#elif defined( _WIN32 )
           "command in cmd.exe syntax; not sh\""
#else
           "command to execute (POSIX sh syntax)\""
#endif
           "}},\"required\":[\"command\"]}";
}

bool takeTerminalTouched()
{
    if( !g_terminalTouched )
        return false;
    g_terminalTouched = false;
    return true;
}

std::string RunCommandTool::execute( const std::string &argumentsJson )
{
    if( !looksJsonObject( argumentsJson ) )
        return "run_command: arguments must be a JSON object";

    std::string command;
    jsonStringArg( argumentsJson, "command", command );
    if( command.empty() )
        return "run_command: 'command' is required";

    // Merge stderr into stdout so the model sees errors too -- and merge it where
    // the platform can actually do it.
#if defined( ATUI_SPAWN_FORK_EXEC )
    // POSIX: redirect the shell's own stderr onto its stdout before the command
    // runs, so even a command that says `1>&2` itself is captured.  `exec` is what
    // makes that stick for everything the shell goes on to run.
    const std::string shellCmd = "exec 2>&1; " + command;
#elif defined( ATUI_SPAWN_WIN32 )
    // Windows: nothing at all, because the child's two output handles are the same
    // handle (see runCaptured) and there is no second stream left to merge.  This
    // used to append `2>&1`, which is worse than nothing: cmd.exe applies
    // redirections left to right, so `echo boom 1>&2 2>&1` points stderr at what
    // is already stderr, and the output captured was empty (measured).
    const std::string shellCmd = command;
#else
    // OS/2 and MS-DOS: the shell is a command processor with no `exec`, and the
    // pipe is a plain popen, so a redirection string is all there is.
    const std::string shellCmd = command + " 2>&1";
#endif

    const long timeoutSeconds = commandTimeoutSeconds();
    const ChildResult result = runCaptured( shellCmd, passEnvNames(),
                                            timeoutSeconds );
    // Marked after the child is done rather than before it starts.  Measured on the
    // host with `sudo ls`: marking beforehand put the repaint one tick after the
    // spawn and still one tick before sudo wrote its password prompt to the
    // terminal, which repairs a screen that has not been broken yet and leaves the
    // damage that follows.  Once the child has finished, every byte it wrote to the
    // screen is behind us and the repaint covers all of it.
    g_terminalTouched = true;
    if( !result.started )
        return "run_command: failed to start '" + oneLineClip( command, 80 ) + "'";

    core::Fmt r;
    if( result.stopRequested )
    {
        r << "stopped before finishing: atui was asked to exit, and everything the "
             "command started was killed with it";
    }
    else if( result.timedOut )
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
        if( out[out.size() - 1] == '\n' ) core::eraseFrom( out, out.size() - 1 );
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
