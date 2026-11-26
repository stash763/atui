// Asking the program to stop, in the one way a signal handler is allowed to do it.
//
// A terminal program is ended by signals more often than by anything its user types:
// the terminal window closing (SIGHUP), `kill` from a script (SIGTERM), Ctrl-C
// (SIGINT).  The default disposition ends the process exactly where it stands, which
// for atui may be in the middle of a streamed answer with a worker thread holding a
// turn, a child process running a tool, and a terminal left in whatever shape the
// last escape sequence put it in.
//
// So the handler does one thing: it sets a flag.  That is not timidity, it is the
// rule -- `volatile sig_atomic_t` is about the only thing a handler may touch, and
// everything else it might do (lock a mutex, allocate, print, wait) is undefined.
// This program has measured the alternative: tvision's handler restores the screen by
// writing to the terminal, and a write blocks when nobody is reading, so a SIGTERM
// delivered while its harness had stopped reading the pty never came back (measured
// 2026-10-22: no process growth, no exit, 0% CPU, forever).
//
// The loop notices at its own pace -- the same 20 Hz tick that pumps a turn -- and
// quits the way the menu does.  That path is the only one that knows how to cancel a
// turn, wait for the worker to finish with it, and put the screen back.
//
// Two doors, one flag.  POSIX has signals.  A Windows console has console control
// events, which are not signals and do not arrive through `signal()`: Ctrl+C, Ctrl+Break,
// the window closing, logoff, shutdown.  `installShutdownHandlers()` registers whichever
// the platform has, and both produce the same flag, so everything downstream -- the tick,
// the loops that wait on a tool's child, the quit path -- is shared code that does not
// know which door the ask came through.  OS/2 has neither SIGTERM nor SIGHUP in its C
// runtime and no console to deliver a break through, so there these functions do nothing
// and nothing changes.
//
// A Windows console handler has to answer a question a POSIX handler never asks: whether
// it claimed the event.  Claiming the first ask (returning TRUE) is what stops Windows
// from terminating the process on the spot, which is the whole point -- the loop then
// leaves the way the menu item does, cancelling the turn and putting the terminal back.
// Refusing a second ask while the first is still unanswered is deliberate: a loop parked
// in a tool's blocking read may not notice the flag for seconds, and a Ctrl+C that
// silently did nothing twice would be worse than the default action.  So asking once
// means "leave properly" and asking again means "now", which is what users already
// expect from the key.
//
// What claiming does not buy, on any console: for close, logoff and shutdown Windows ends
// the process when the handler returns, so whether the loop gets its turn is a race.  The
// handler does not wait to win that race.  Blocking in a handler is how a program becomes
// unhung-able, and this program has measured exactly that failure on POSIX (see above).
#ifndef ATUI_CORE_SIGNALS_H
#define ATUI_CORE_SIGNALS_H

namespace atui {
namespace core {

// Installs the handlers.  Call this before the user interface is built: tvision saves
// whatever disposition is in force when it takes the console over and invokes it from
// its own handler, so being installed first is what makes both things happen -- the
// screen gets restored and the flag gets set -- instead of one replacing the other.
// Calling it twice is harmless.
void installShutdownHandlers();

// True once a stop has been asked for.  Cheap enough to ask every tick.
bool shutdownRequested();

// Forgets the request, so one signal cannot end the program twice.
void clearShutdownRequest();

#if defined( _WIN32 )
// The body of the console control handler: reports whether `eventCode` -- a Windows
// `CTRL_*_EVENT` -- is an ask atui recognises, and claims it.  Returns true for the first
// recognised ask, false for one that is not an ask and for a second ask whose first is
// still unanswered.
//
// It is a function rather than inline in the `BOOL WINAPI` shim for one reason: the shim
// can only be reached by sending a control event to every process attached to this
// console, which would end the test run with it.  Called by Windows, and by the tests, on
// exactly the argument Windows would pass.
bool noteConsoleCtrlEvent( unsigned long eventCode );
#endif

} // namespace core
} // namespace atui

#endif // ATUI_CORE_SIGNALS_H
