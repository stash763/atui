// A non-blocking TCP client, in the handful of calls an event loop needs.
//
// atui talks to one server, on a LAN, over plain HTTP, and it has to do that from
// inside a TUI: the screen has to keep drawing while a model thinks, and a click
// has to reach the transcript.  So the transport is a state machine rather than a
// call that blocks, and these are the calls it drives it with: start a connection,
// ask whether it is ready, move some bytes, close.
//
// The shape is chosen for the platforms, not against them.  Everything here is
// per-instance and takes no lock, and the stack start-up is idempotent, so a host
// with threads may own one of these per thread; and the state machine is what a
// single stack doing several things -- an event loop, or DOS work driven from a timer
// hook -- needs anyway.  What this build does not have is a thread *runtime*: Open
// Watcom's OS/2 target ships no `<thread>` or `<atomic>`, which is a fact about the
// headers and not about OS/2, Windows or DOS (see PORTING.md).
//
// Why a layer at all, when every platform has `socket()`: because they agree about
// almost nothing else, and each disagreement is one that breaks a program if it is
// assumed rather than handled.  Measured on the OS/2 target, and written up in
// TESTING.md:
//
//   * a POSIX socket raises SIGPIPE at the first write to a connection the peer has
//     closed, and the default disposition terminates the process; OS/2 and Windows
//     have no SIGPIPE.  Handled per socket in `sock_posix.cpp` rather than by
//     ignoring the signal for the process, because an ignored SIGPIPE is inherited by
//     every command atui runs.  Measured, with the numbers, in TESTING.md;
//   * a socket is an `int` there and it is closed with `soclose()`.  The C
//     library's `close()` returns -1 for a socket handle -- every time, so closing
//     one that way leaks it, and the leak shows up as "cannot open socket" long
//     after the change that caused it;
//   * `errno` is not maintained for socket calls, and `<errno.h>` has none of the
//     BSD socket errors.  The stack's own number comes from `sock_errno()`: 10035
//     for "nothing to read yet", 10061 for "connection refused";
//   * a non-blocking `connect()` returns -1 while succeeding -- select() for
//     writing followed by `SO_ERROR` is what says whether it came up;
//   * there is no `O_NONBLOCK` in `<fcntl.h>`: non-blocking is
//     `ioctl(s, FIONBIO, &on)`.
//
// Windows is the third member of this family with its own spellings (`SOCKET`,
// `closesocket`, `WSAGetLastError`, `WSAStartup`), and DOS with a packet driver
// will be the fourth.  None of that belongs above this line: the state machine
// below asks for bytes and is told "not yet", "the peer is finished", or "here".
//
// Nothing here starts a thread or waits for anything except the timeout it is
// given, because it is called from the code that owns the screen.

#ifndef ATUI_NET_SOCK_H
#define ATUI_NET_SOCK_H

#include <string>
#include <stddef.h>   // size_t

namespace atui {
namespace net {

// What a call that could not finish has to say.  The distinction between the first
// three is the whole point: "not yet" and "the peer is finished" look the same to
// a caller that treats -1 as one answer, and one of them ends a request while the
// other must not.
enum SockWhy
{
    srOk      = 0,   // the call did what was asked
    srAgain   = 1,   // not now: nothing to read, or the send buffer is full
    srClosed  = 2,   // the peer is finished; reading again will keep saying so
    srTimeout = 3,   // a wait expired with nothing happening
    srError   = 4    // it failed, and `reason` says so in words
};

// A socket handle.  `int` where sockets are descriptors, and wide enough for
// Windows' `SOCKET`, which is a pointer-sized integer.
#ifdef _WIN32
typedef unsigned long long SockHandle;
const SockHandle kNoSocket = (SockHandle) -1;   // all ones, as INVALID_SOCKET is
#else
typedef int SockHandle;
const SockHandle kNoSocket = -1;
#endif

// Any library setup the platform needs before the first socket.  Idempotent, and
// called by the functions below as well, so a caller only has to call it if it
// wants to know whether the stack is there at all.
void sockGlobalInit();

// Whether the TCP/IP stack can be used.  On OS/2 this is `sock_init()` answering,
// and the answer matters: without a stack the program still has a UI, and "no
// TCP/IP on this machine" is a better thing to say than a request that fails in a
// way nobody recognises.
bool sockStackReady( std::string *reason );

// `host` in numeric form -- a dotted quad -- or empty if it cannot be resolved.
// `host` may already be numeric, which costs nothing: the stack parses it without
// asking anyone.  This is the one call here that may block, on a name server that
// is not answering; the timeout on the request is what bounds it.
std::string sockNumericAddress( const std::string &host );

// Opens a socket and starts connecting to `host`: `port`, returning as soon as the
// attempt is under way.  A connection still in progress is success, not a failure:
// wait for writability and then read `sockError()`, which is empty if the
// connection came up and says what went wrong if it did not.  Returns kNoSocket
// only when no attempt was made, with `reason` filled.
SockHandle sockConnectStart( const std::string &host, int port, std::string *reason );

// Waits up to `ms` milliseconds (0 to look and return immediately) for one
// condition.  Returns srOk when it is worth calling again, srTimeout when the wait
// expired, srError when select() itself failed.
SockWhy sockWait( SockHandle handle, bool forRead, int ms, std::string *reason );

// Writes as much of `data` as the stack will take; `written` says how much, and
// srAgain means the buffer was full and to try again when it is writable.
SockWhy sockSend( SockHandle handle, const char *data, size_t len,
                  size_t *written, std::string *reason );

// Reads up to `cap` bytes into `buf`; `got` says how many.  srClosed means the
// peer has finished sending -- that is recv() returning 0, which is a different
// answer from srAgain and must never be treated as it.
SockWhy sockRecv( SockHandle handle, char *buf, size_t cap,
                  size_t *got, std::string *reason );

// The socket's own error as words, empty when there is none.  Where errno is not
// maintained (OS/2), this is the only account of why a connect failed; on the
// other platforms it is the same words a caller would otherwise have to build.
std::string sockError( SockHandle handle );

// Ends the connection and gives the handle back.  With one exception, and that
// exception is the reason this is a function rather than a call the caller writes:
// on OS/2 the wrong spelling does not fail loudly, it returns -1 and leaves the
// socket open.
void sockClose( SockHandle handle );

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_SOCK_H
