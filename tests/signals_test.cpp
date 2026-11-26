// Tests for the shutdown-request flag.
//
// The interesting property is not that a flag gets set -- it is that nothing else
// happens. A handler that tries to be helpful (print, lock, flush, exit) is a handler
// that runs on a random instruction with the program's locks in its hands, and this
// program has measured what that costs: a handler that writes to a terminal nobody is
// reading blocks forever, and the signal that invoked it is never finished.  So these
// tests assert the flag, and assert that the process is still here to notice it --
// which is the same assertion made the only way it can be made, by reaching the next
// line.
//
// The ask arrives through one of two doors -- a POSIX signal, or a Windows console
// control event -- and the file is organised around that: the tests that are about a
// door are written for that door, and the tests that are about what the flag does are
// written once, behind `ask_for_stop()`.  Every platform that can be asked to stop
// registers the same four properties.  The two that cannot (OS/2, MS-DOS) register a
// skip that says why, rather than registering nothing and leaving the suite quietly
// smaller than another platform's.
#include "harness.h"
#include "core/signals.h"
#include "core/thread.h"
#include "tools/run_command_tool.h"

#if !defined( __OS2__ ) && !defined( __DOS__ ) && !defined( _WIN32 )
#  define ATUI_TEST_SIGNALS 1
#endif
#if defined( _WIN32 )
#  define ATUI_TEST_CONSOLE_CTRL 1
#endif

#if defined( ATUI_TEST_SIGNALS ) || defined( ATUI_TEST_CONSOLE_CTRL )
// `waited` is measured on the wall clock: std::clock() counts CPU, and while a child
// process sleeps the program uses none, so it would report a fast exit either way.
#  include <chrono>
#  include <string>
#endif

#if defined( ATUI_TEST_SIGNALS )
#  include <csignal>
#  include <unistd.h>          // usleep, for the one wait below
#endif

#if defined( ATUI_TEST_CONSOLE_CTRL )
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>         // the CTRL_*_EVENT constants, so the tests pass exactly
                               // the argument Windows would pass
#endif

namespace atui_test {

namespace {

#if defined( ATUI_TEST_SIGNALS )

// What the platform's disposition was, so a test can put it back.
struct SavedHandlers
{
    void (*term)( int );
    void (*hup)( int );
    void (*inte)( int );

    SavedHandlers()
        : term( std::signal( SIGTERM, SIG_DFL ) )
        , hup( std::signal( SIGHUP, SIG_DFL ) )
        , inte( std::signal( SIGINT, SIG_DFL ) )
    {
        // `signal()` returns the previous handler, or SIG_ERR on failure.  Restoring
        // SIG_ERR is not possible, so treat it as "leave it at default".
        if( term == SIG_ERR ) term = SIG_DFL;
        if( hup == SIG_ERR ) hup = SIG_DFL;
        if( inte == SIG_ERR ) inte = SIG_DFL;
    }

    ~SavedHandlers()
    {
        std::signal( SIGTERM, term );
        std::signal( SIGHUP, hup );
        std::signal( SIGINT, inte );
    }
};

// SIGTERM asks, it does not take.
int test_sigterm_asks_and_survives()
{
    SavedHandlers saved;
    atui::core::installShutdownHandlers();
    atui::core::clearShutdownRequest();

    if( atui::core::shutdownRequested() )
        return -1;                       // a request left over from something else
    if( std::raise( SIGTERM ) != 0 )
        return -2;
    if( !atui::core::shutdownRequested() )
        return -3;                       // handler did not run, or ran on a different flag
    atui::core::clearShutdownRequest();
    if( atui::core::shutdownRequested() )
        return -4;                       // and it can be forgotten, so it fires once
    return 0;
}

// The other two ways a terminal program dies, same answer.
int test_hangup_and_interrupt_ask_too()
{
    SavedHandlers saved;
    atui::core::installShutdownHandlers();
    atui::core::clearShutdownRequest();

    if( std::raise( SIGHUP ) != 0 )
        return -2;
    if( !atui::core::shutdownRequested() )
        return -3;
    atui::core::clearShutdownRequest();

    if( std::raise( SIGINT ) != 0 )
        return -4;
    if( !atui::core::shutdownRequested() )
        return -5;
    atui::core::clearShutdownRequest();
    return 0;
}

// The default action must be gone: the whole point is that the process decides when to
// leave rather than being stopped on the instruction the signal landed on.
int test_the_default_action_is_replaced()
{
    SavedHandlers saved;                    // saves whatever it is now, restores after
    atui::core::installShutdownHandlers();

    void (*prev)( int ) = std::signal( SIGTERM, SIG_DFL );
    const bool wasOurs = ( prev != SIG_DFL && prev != SIG_ERR );
    std::signal( SIGTERM, prev == SIG_ERR ? SIG_DFL : prev );
    return wasOurs ? 0 : -1;
}

#endif // ATUI_TEST_SIGNALS

#if defined( ATUI_TEST_CONSOLE_CTRL )

// A Windows console control handler is added to a list rather than installed over what
// was there, so there is no previous disposition to restore and this test fixture is
// honestly empty.  It exists under the same name so the tests that are about the flag
// read the same on both platforms.
struct SavedHandlers {};

// Ctrl+C asks, it does not take.
//
// What these tests cannot do is send a real Ctrl+C: a console control event goes to every
// process attached to the console, the test runner among them, so the ask is made through
// the function the handler's shim calls, on the argument Windows would pass it.  That the
// event arrives, and that claiming it keeps the process alive long enough to leave
// properly, is measured on the desktop and recorded in TESTING.md.
int test_being_asked_to_stop()
{
    SavedHandlers saved;
    atui::core::installShutdownHandlers();
    atui::core::clearShutdownRequest();

    if( atui::core::shutdownRequested() )
        return -1;                       // a request left over from something else
    if( !atui::core::noteConsoleCtrlEvent( CTRL_C_EVENT ) )
        return -2;                       // refused: Ctrl+C would do nothing at all
    if( !atui::core::shutdownRequested() )
        return -3;                       // claimed, and the flag it exists for is down
    atui::core::clearShutdownRequest();
    if( atui::core::shutdownRequested() )
        return -4;                       // and it can be forgotten, so it fires once
    return 0;
}

// Break, close, logoff and shutdown are the other ways a console ends, same answer.  And
// an event code atui has not read is not claimed, because a handler that claims an event
// it has not looked at is telling the operating system what the process intends to do
// without having read the request.
int test_the_other_console_events_ask_too()
{
    SavedHandlers saved;
    atui::core::installShutdownHandlers();

    const unsigned long asks[] = {
        CTRL_BREAK_EVENT, CTRL_CLOSE_EVENT, CTRL_LOGOFF_EVENT, CTRL_SHUTDOWN_EVENT
    };
    for( size_t i = 0; i < sizeof( asks ) / sizeof( asks[0] ); ++i )
    {
        atui::core::clearShutdownRequest();
        if( !atui::core::noteConsoleCtrlEvent( asks[i] ) )
            return (int) ( -10 - (int) i );      // this console event asks nothing
        if( !atui::core::shutdownRequested() )
            return (int) ( -20 - (int) i );      // claimed without setting the flag
    }

    atui::core::clearShutdownRequest();
    if( atui::core::noteConsoleCtrlEvent( 0x12345UL ) )
        return -31;                      // claimed an event it cannot know
    if( atui::core::shutdownRequested() )
        return -32;                      // and pulled the flag with it
    return 0;
}

// On this platform the default action is what Windows does when no handler claims the
// event, so "the default action is replaced" means: the first ask is claimed, and the
// process decides when to leave.  The other half is asserted too -- a second ask while
// the first is unanswered is refused, so a loop that cannot notice the flag is not made
// unreachable by the thing that was supposed to stop it politely.
int test_the_default_action_is_replaced()
{
    SavedHandlers saved;
    atui::core::installShutdownHandlers();
    atui::core::clearShutdownRequest();

    if( !atui::core::noteConsoleCtrlEvent( CTRL_C_EVENT ) )
        return -1;                       // first ask not claimed: Windows ends the process
    if( atui::core::noteConsoleCtrlEvent( CTRL_C_EVENT ) )
        return -2;                       // second ask claimed: nothing left to escalate to
    atui::core::clearShutdownRequest();
    if( !atui::core::noteConsoleCtrlEvent( CTRL_C_EVENT ) )
        return -3;                       // a forgotten ask is claimable again
    atui::core::clearShutdownRequest();
    return 0;
}

#endif // ATUI_TEST_CONSOLE_CTRL

#if defined( ATUI_TEST_SIGNALS ) || defined( ATUI_TEST_CONSOLE_CTRL )

// The door, in one line per platform.
bool ask_for_stop()
{
#if defined( ATUI_TEST_SIGNALS )
    return std::raise( SIGTERM ) == 0;
#else
    return atui::core::noteConsoleCtrlEvent( CTRL_C_EVENT );
#endif
}

// And the pause, likewise.
void nap_ms( unsigned ms )
{
#if defined( ATUI_TEST_SIGNALS )
    usleep( ms * 1000 );
#else
    Sleep( ms );
#endif
}

// The flag exists so that code a long way from the handler can read it.  The distance
// that matters is a tool's child process: waiting for one is the longest thing atui does
// on the user's behalf, and a stop request has to reach the loop holding the pipe open
// or "asked to leave" quietly becomes "waited for the command to finish".  Measured with
// `sleep 40` running: 40.2s to exit before the loop checked, 0.59s after, and 40.2s
// again after the tick went back to clearing the flag it had just acted on -- which is
// how a design mistake in one place turns into a wait in another.
class LongCommand : public atui::core::Task
{
public:
    virtual void run()
    {
        result_ = atui::tools::g_runCommandTool.execute(
            "{\"command\":\"" + wait_command( 5 ) + "\"}" );
    }

    const std::string &result() const { return result_; }

private:
    std::string result_;
};

int test_a_stop_request_reaches_a_running_command()
{
    if( !atui::core::threadsAvailable() )
        return 0;                           // with no second stack the command is on
                                            // the thread that has to be left, and
                                            // there is nothing across from it

    SavedHandlers saved;
    atui::core::installShutdownHandlers();
    atui::core::clearShutdownRequest();

    LongCommand work;
    atui::core::Thread thread;
    std::string reason;
    if( !thread.start( work, &reason ) )
        return -1;

    // Letting the command start is a sleep rather than a wait on a predicate, because
    // the tool owns the only evidence that it has. It is also not load-bearing: with
    // the flag already set the loop stops on its first pass, and with it set later it
    // stops on the next, so either timing has to produce what is checked below.
    nap_ms( 300 );

    const std::chrono::steady_clock::time_point asked =
        std::chrono::steady_clock::now();
    if( !ask_for_stop() )
    {
        thread.join();                      // never leave a thread running behind a
        return -2;                          // test that could not start the check
    }
    thread.join();
    const long waitedMs = (long) std::chrono::duration_cast< std::chrono::milliseconds >(
        std::chrono::steady_clock::now() - asked ).count();

    atui::core::clearShutdownRequest();     // here the test is the consumer, which is
                                            // exactly what the app's tick is not
    if( waitedMs > 3000 )
        return (int) ( -10000 - waitedMs ); // it sat out the command: the number is
                                            // the wait command saying it was not stopped
    if( work.result().find( "stopped before finishing" ) == std::string::npos )
        return -3;
    return 0;
}

#else  // this platform cannot be asked to stop

// OS/2 has neither SIGTERM nor SIGHUP in its C runtime and no console that delivers a
// break as an event, and MS-DOS has neither mechanism at all, so there is nothing here to
// assert -- but the absence belongs on the suite's own line rather than in a count nobody
// compares.
int test_being_asked_to_stop()
{
    return skip_test( "no way to ask: this platform has neither SIGTERM nor console "
                      "control events" );
}

#endif // a platform that can be asked

} // anonymous namespace

// Registers the shutdown tests.
void init_signals_tests()
{
#if defined( ATUI_TEST_SIGNALS )
    register_test( "signal: a stop request asks and survives", &test_sigterm_asks_and_survives );
    register_test( "signal: hangup and interrupt ask too", &test_hangup_and_interrupt_ask_too );
    register_test( "signal: the default action is replaced", &test_the_default_action_is_replaced );
    register_test( "signal: a stop request reaches a running command",
                   &test_a_stop_request_reaches_a_running_command );
#elif defined( ATUI_TEST_CONSOLE_CTRL )
    // The same four properties as every other platform, two of them named the same and
    // two of them named for the door: what a stop is asked with differs here, what it
    // causes does not.
    register_test( "signal: a stop request asks and survives", &test_being_asked_to_stop );
    register_test( "signal: the other console events ask too",
                   &test_the_other_console_events_ask_too );
    register_test( "signal: the default action is replaced", &test_the_default_action_is_replaced );
    register_test( "signal: a stop request reaches a running command",
                   &test_a_stop_request_reaches_a_running_command );
#else
    register_test( "signal: being asked to stop", &test_being_asked_to_stop );
#endif
}

} // namespace atui_test
