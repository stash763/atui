// Tests for the shutdown-signal flag.
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
// POSIX can be asked to stop and so runs the four tests.  The platforms that cannot be
// asked -- Windows, OS/2, MS-DOS -- register a skip that says what is missing on that
// platform and why, rather than registering nothing and leaving the suite quietly
// smaller than another platform's.  Windows is the interesting case: it has a mechanism
// that looks like the answer and was tried, and `core/signals.h` keeps the measurements
// that took it back out.
#include "harness.h"
#include "core/signals.h"
#include "core/thread.h"
#include "tools/run_command_tool.h"

#if !defined( __OS2__ ) && !defined( __DOS__ ) && !defined( _WIN32 )
#  define ATUI_TEST_SIGNALS 1
#endif

#if defined( ATUI_TEST_SIGNALS )
// `waited` is measured on the wall clock: std::clock() counts CPU, and while a child
// process sleeps the program uses none, so it would report a fast exit either way.
#  include <chrono>
#  include <csignal>
#  include <string>
#  include <unistd.h>          // usleep, for the one wait below
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
        result_ = atui::tools::g_runCommandTool.execute( "{\"command\":\""
                                                         + wait_command( 5 ) + "\"}" );
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
    usleep( 300 * 1000 );

    const std::chrono::steady_clock::time_point asked =
        std::chrono::steady_clock::now();
    if( std::raise( SIGTERM ) != 0 )
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

#else  // nothing here can ask atui to stop

// Windows has console control events, which look like the answer, and atui installed a
// handler for them on 25 Nov 2026.  It did nothing and was taken back out the same day:
// tvision clears `ENABLE_PROCESSED_INPUT` so Ctrl+C reaches it as the key 0x03 rather
// than as an event (measured with the tick traced: no handler call, no echo, no stop,
// ticks still at 20 Hz); Ctrl+C must not be taken as a stop request in any case, because
// on this platform it is the copy key, and leaving is what Alt-X and the menu's Exit item
// are for -- one command, and Alt-X leaving with status 0 is measured on this console;
// and the events
// that do arrive -- close, logoff, shutdown -- end the process when the handler returns,
// so the loop never reads the flag (measured: a closed console window killed a traced
// atui with no stop line).  Ctrl+Break would work and is not implemented because nothing
// available can generate it: the keyboard injection cannot synthesise Break, and no other
// process can attach to atui's console to ask (error 32).
//
// OS/2 and MS-DOS are the same story with fewer details -- no SIGTERM or SIGHUP in the C
// runtime, and no console delivering anything like an event -- so they register this same
// skip in their own words.
int test_being_asked_to_stop()
{
#if defined( _WIN32 )
    return skip_test( "Windows: Ctrl+C is the copy key and arrives as a key, not an event, "
                      "and the console events that do arrive end the process before the "
                      "loop can read a flag -- exiting is Alt-X or the menu's Exit item" );
#else
    return skip_test( "no way to ask: this platform has neither SIGTERM nor console "
                      "control events" );
#endif
}

#endif // ATUI_TEST_SIGNALS

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
#else
    // OS/2 has neither SIGTERM nor SIGHUP in its C runtime and no console that delivers a
    // break as an event, and MS-DOS has neither mechanism at all.  Windows is explained
    // above, at the test that reports it.
    register_test( "signal: being asked to stop", &test_being_asked_to_stop );
#endif
}

} // namespace atui_test
