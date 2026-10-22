// Shutdown signals.  The reasoning is in `signals.h`.
#include "signals.h"

// Which platforms get real handlers.  OS/2's C runtime has no SIGTERM or SIGHUP to
// install, and Windows consoles deliver a CTRL_C_EVENT through a different door
// entirely; on those the flag stays down and the program exits the way it always has.
#if !defined( __OS2__ ) && !defined( __DOS__ ) && !defined( _WIN32 )
#  define ATUI_HAVE_SIGNALS 1
#endif

#if defined( ATUI_HAVE_SIGNALS )
#  include <csignal>
#endif

namespace atui {
namespace core {

#if defined( ATUI_HAVE_SIGNALS )

namespace {

// The one variable this program is allowed to share with a signal.
volatile sig_atomic_t g_stop = 0;

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

bool shutdownRequested()
{
    return g_stop != 0;
}

void clearShutdownRequest()
{
    g_stop = 0;
}

#else  // no signals

void installShutdownHandlers() {}
bool shutdownRequested() { return false; }
void clearShutdownRequest() {}

#endif // ATUI_HAVE_SIGNALS

} // namespace core
} // namespace atui
