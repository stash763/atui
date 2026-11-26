// Shutdown signals.  The reasoning is in `signals.h`.
#include "signals.h"

// Which platforms can be asked to stop, and through which door.  POSIX has signals.  A
// Windows console delivers Ctrl+C as a console control event, which never passes through
// `signal()`, so it needs its own registration and gets its own door below.  OS/2's C
// runtime has no SIGTERM or SIGHUP to install and no console to deliver a break through,
// and MS-DOS has neither: there the flag stays down and the program exits the way it
// always has.
#if defined( _WIN32 )
#  define ATUI_HAVE_CONSOLE_CTRL 1
#elif !defined( __OS2__ ) && !defined( __DOS__ )
#  define ATUI_HAVE_SIGNALS 1
#endif

// Both doors set the same flag, and `sig_atomic_t` is what both the signal handler and a
// console control handler are allowed to write.
#if defined( ATUI_HAVE_SIGNALS ) || defined( ATUI_HAVE_CONSOLE_CTRL )
#  include <csignal>
#endif

#if defined( ATUI_HAVE_CONSOLE_CTRL )
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace atui {
namespace core {

#if defined( ATUI_HAVE_SIGNALS ) || defined( ATUI_HAVE_CONSOLE_CTRL )

namespace {

// The one variable this program is allowed to share with a handler.
volatile sig_atomic_t g_stop = 0;

} // anonymous namespace

bool shutdownRequested()
{
    return g_stop != 0;
}

void clearShutdownRequest()
{
    g_stop = 0;
}

#endif // a stop can be asked for

#if defined( ATUI_HAVE_SIGNALS )

namespace {

void onStop( int )
{
    g_stop = 1;
}

} // anonymous namespace

void installShutdownHandlers()
{
    // SIGHUP: the terminal went away, which used to mean atui kept running against a
    // dead console.  SIGTERM: a script asked politely.  SIGINT: Ctrl-C, which in a
    // full-screen program should mean "leave properly" -- the same thing the menu
    // item means -- rather than "die on this instruction".
    //
    // SIGQUIT is deliberately not included: a core dump is what somebody asking for
    // one wants, and the default action gives it to them.
    std::signal( SIGTERM, onStop );
    std::signal( SIGHUP, onStop );
    std::signal( SIGINT, onStop );
}

#endif // POSIX signals

#if defined( ATUI_HAVE_CONSOLE_CTRL )

bool noteConsoleCtrlEvent( unsigned long eventCode )
{
    switch( eventCode )
    {
        case CTRL_C_EVENT:       // Ctrl+C
        case CTRL_BREAK_EVENT:   // Ctrl+Break, which cannot be remapped away
        case CTRL_CLOSE_EVENT:   // the console window is going away
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            break;
        default:
            return false;         // not an ask, so not ours to claim
    }

    // An ask that is already unanswered means atui is on its way out and has not got
    // there yet.  Claiming a second ask would keep it from being reachable by any other
    // means, so the second one is left for Windows to act on.
    if( g_stop != 0 )
        return false;

    g_stop = 1;
    return true;
}

namespace {

BOOL WINAPI onConsoleEvent( DWORD type )
{
    return noteConsoleCtrlEvent( (unsigned long) type ) ? TRUE : FALSE;
}

} // anonymous namespace

void installShutdownHandlers()
{
    // Added rather than replaced: `SetConsoleCtrlHandler( ..., TRUE )` appends to the
    // list, so this cannot displace a handler tvision or the runtime put in front of it --
    // which is the property the POSIX install relies on for a different reason (the
    // comment in `signals.h` and in the POSIX install above).
    //
    // Failing is possible and means the process has no console at all, which is where a
    // run from a scheduler or an SSH session without a pseudoconsole stands.  Nothing is
    // reported: the behaviour is then exactly what it was before this function existed,
    // and a program with no console has nowhere honest to say it.
    SetConsoleCtrlHandler( &onConsoleEvent, TRUE );
}

#endif // Windows console control events

#if !defined( ATUI_HAVE_SIGNALS ) && !defined( ATUI_HAVE_CONSOLE_CTRL )

void installShutdownHandlers() {}
bool shutdownRequested() { return false; }
void clearShutdownRequest() {}

#endif // no way to be asked

} // namespace core
} // namespace atui
