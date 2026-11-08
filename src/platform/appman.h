// The hands seam: what this machine can do to other programs.
//
// The eyes (screen.h) read the desktop; this reads nothing and acts on
// it -- launch a program, close a window, press a button. Each
// capability is answered by the backends (appman_os2.cpp today,
// appman_none.cpp everywhere else, X11/XTEST some future day) and each
// gates its own tool at registration, because a model offered hands the
// machine does not have is worse than a model offered no hands.
//
// What lives here is bounded by measurement, and the measurements are
// unkind ones (TESTING.md keeps the journal):
//
//   * Hardware-level key injection is out -- the box lacks the tool
//     DLL that would provide it, and nothing else on OS/2 writes the
//     real input stream from user mode.
//   * Dispatching WM_CHAR to a foreign toolkit control is out: it
//     works, and intermittently kills the target inside its own MLE
//     code (instrumented: the pump dies inside WinDispatchMsg, no
//     crash report, the same bytes surviving minutes earlier). Only
//     the device path is legal input, and that path is not reachable.
//   * Pointer-carrying control messages are out: PM marshals pointers
//     for system messages only, so an off-process MLM_EXPORT asks a
//     foreign process to memcpy into an address from another world.
//
// What remains, proven safe on live targets: start sessions, post
// WM_CLOSE, post WM_COMMAND to owners, and WinSetWindowText on
// controls that accept it. Cooperative apps (the atui family) may
// later add a private door for text -- in-process insertion is the one
// path proven harmless.
//
// Handles are the same opaque strings the windows tool prints, so
// "which window" is one vocabulary.
//
// C++98, standard library only.
#ifndef ATUI_PLATFORM_APPMAN_H
#define ATUI_PLATFORM_APPMAN_H

#include <string>

namespace atui {
namespace platform {

// --- Capabilities. Each gates one tool at registration. ---

// Whether programs can be started here at all (OS/2: DosStartSession).
bool appLaunchAvailable();

// Whether windows can be acted on here (OS/2: a PM session answers
// WinPostMsg). A yes is an invitation to try; the calls still answer
// with errors.
bool appPostAvailable();

// --- The acts. ---

// Starts `program` (an absolute path the caller checked exists). On
// success `how` says what the machine did -- foreground or background
// -- because on a machine whose sessions do not all own the display
// the difference is visible to the user and invisible to this call.
// On failure `error` carries the API's number, which is how the
// 457-from-a-background-anchor story got found the first time.
bool appLaunch( const std::string &program,
                std::string &how,
                unsigned long &pid,
                std::string &error );

// Posts WM_CLOSE at the window -- the polite door, and the one that
// lets the target save its work. `handle` is the opaque string form.
bool appCloseWindow( const std::string &handle, std::string &error );

// Presses a control's button for it: WM_COMMAND with the control's
// own id (QWS_ID) and a notification of 0x0100 (the value standard
// PM buttons announce as BN_CLICKED). Only controls that answer by
// command id do anything; this does nothing to windows that drive
// themselves by mouse messages.
bool appClickWindow( const std::string &handle, std::string &error );

// Replaces a window's text through the marshaled system door
// (WinSetWindowText). SPIN controls mostly refuse it; plain edits and
// titles accept it. Honest failure beats forged keys.
bool appSetWindowText( const std::string &handle,
                       const std::string &text,
                       std::string &error );

// The expert door: one posted message, value-only parameters. The
// description of the tool that wraps it carries the MLE landmine in
// writing; the machine will not stop this call from stepping on it.
bool appPostMessage( const std::string &handle,
                     unsigned long msg,
                     unsigned long mp1,
                     unsigned long mp2,
                     std::string &error );

// Sleep, because acting and looking need breaths between them. The
// tools that watch for a window to appear would otherwise spin.
void appSleepMs( int ms );

// --- Pure parts, testable without a desktop. ---

// Parses a window handle string: "0x80000207" (the form the windows
// tool prints) or plain decimal. A handle of zero is not a window:
// rejected, like everything that is not a number.
bool parseWindowHandle( const std::string &text, unsigned long &out );

// Parses a message/parameter word for the escape-hatch tool: the same
// rules as parseWindowHandle except zero is allowed.
bool parseMessageWord( const std::string &text, unsigned long &out );

} // namespace atui::platform
} // namespace atui

#endif // ATUI_PLATFORM_APPMAN_H
