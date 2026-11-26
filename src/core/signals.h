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
// POSIX, and only POSIX -- which is a measured answer rather than an omission.  A Windows
// console has console control events instead of signals, and this project tried that door
// (installed 25 Nov 2026, withdrawn the same day) and found nothing behind it:
//
//   - Ctrl+C does not arrive.  tvision clears `ENABLE_PROCESSED_INPUT` so that Ctrl+C
//     comes through as the key 0x03 rather than as an event, and with a handler installed
//     and the tick traced, pressing Ctrl+C produced no handler call, no echo and no stop
//     while the ticks went on at 20 Hz.
//   - Nor should it be an ask.  On Windows, OS/2 and DOS Ctrl+C is the copy key, and a
//     program that leaves on it has taken a clipboard shortcut away from the person using
//     it.  atui leaves exiting to Alt-X and the menu's Exit item, which post the same
//     command, and Alt-X leaving with status 0 and the screen put back is measured on the
//     target (`TESTING.md`, the `P4` table).
//   - Close, logoff and shutdown do arrive and claiming them buys nothing: Windows ends
//     the process when the handler returns, so the loop never gets its turn (measured: a
//     console window closed over a traced atui died with no stop line while ticks ran).
//
// Ctrl+Break is the one event that would work.  It is not installed because it cannot be
// measured from here: the keyboard injection this project has cannot synthesise Break, and
// no other process can attach to atui's console to generate the event (error 32, at any
// share mode).  Code for a case that cannot be shown is not worth its cost.
//
// OS/2 has neither SIGTERM nor SIGHUP in its C runtime and no console to deliver a break
// through; MS-DOS has neither mechanism at all.  There these functions do nothing and
// nothing changes.
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

} // namespace core
} // namespace atui

#endif // ATUI_CORE_SIGNALS_H
