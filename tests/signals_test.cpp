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
#include "harness.h"
#include "core/signals.h"

#if !defined( __OS2__ ) && !defined( __DOS__ ) && !defined( _WIN32 )
#  define ATUI_TEST_SIGNALS 1
#endif

#if defined( ATUI_TEST_SIGNALS )
#  include <csignal>
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

} // anonymous namespace

// Registers the shutdown tests.
void init_signals_tests()
{
#if defined( ATUI_TEST_SIGNALS )
    register_test( "signal: a stop request asks and survives", &test_sigterm_asks_and_survives );
    register_test( "signal: hangup and interrupt ask too", &test_hangup_and_interrupt_ask_too );
    register_test( "signal: the default action is replaced", &test_the_default_action_is_replaced );
#endif
}

} // namespace atui_test
