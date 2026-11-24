// The socket seam for the test harness: the handful of spellings a scripted server
// needs, and nothing more.
//
// `http_pipe.h` is a server that plays a script, so it needs six things: load the
// stack, make a listening socket, wait for something with a timeout, read, write, and
// close.  Those six are exactly where a POSIX stack and Winsock disagree, and the
// disagreement about closing is not the quiet kind: a Winsock socket is a pointer-sized
// value rather than a descriptor, and handing one to the C library's `close()` does not
// return -1 the way it does on OS/2 -- the UCRT treats a descriptor it does not own as
// an invalid parameter and fails the process fast, 0xC0000409 (measured, and the same
// trap `sock.h` records for the product's own layer).  Naming the six here is what keeps
// the platform question out of every test that wants a server.
//
// What is deliberately *not* here is the product's own layer (`src/net/sock.h`).  It
// would be less code to reuse it, and the reuse would be worthless: the harness exists
// to talk to that code from outside, and if both sides of a conversation were written
// by the same six functions then a misunderstanding between them has no way to show
// up.  So the harness speaks Winsock or POSIX directly, the transport speaks its own
// layer, and a test passes only when the two agree over a real socket.
//
// Only the differences this harness actually meets are handled.  There is no `poll()`
// on Windows, so `pipeReadable` is `select()`, which Winsock has and which behaves the
// same for one socket; `socklen_t` and `ssize_t` are not what every SDK spells them,
// hence the two aliases.
//
// C++14, tests only, like the rest of this directory.
#ifndef ATUI_TESTS_PIPE_SOCK_H
#define ATUI_TESTS_PIPE_SOCK_H

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace atui_test {

#ifdef _WIN32

typedef SOCKET PipeFd;
typedef int PipeLen;      // the SDK added `socklen_t` late; `int` is what it names
typedef int PipeCount;    // Winsock's read and write return `int`, and mean it

const PipeFd kPipeInvalid = (PipeFd) INVALID_SOCKET;

inline int pipeClose( PipeFd fd ) { return closesocket( fd ); }

// Winsock refuses every call until it has been asked for, and the reference counting
// is Winsock's, not ours: repeated calls are harmless and no `WSACleanup()` follows,
// because a test process that exits releases the stack anyway.
inline bool pipeStackInit()
{
    static WSADATA wsa;
    return WSAStartup( MAKEWORD( 2, 2 ), &wsa ) == 0;
}

// Only to wake the accept wait from another thread, which is safe to do while that
// thread is waiting and is not safe with `closesocket()`.  The close comes after the
// thread has been joined -- see `ScriptedHttp::stop()`.
inline void pipeWake( PipeFd fd ) { ::shutdown( fd, SD_BOTH ); }

inline bool pipeReadable( PipeFd fd, int ms )
{
    fd_set set;
    FD_ZERO( &set );
    FD_SET( fd, &set );
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = ( ms % 1000 ) * 1000;
    return ::select( 0, &set, 0, 0, &tv ) > 0;
}

#else

typedef int PipeFd;
typedef socklen_t PipeLen;
typedef ssize_t PipeCount;

const PipeFd kPipeInvalid = -1;

inline int pipeClose( PipeFd fd ) { return ::close( fd ); }
inline bool pipeStackInit() { return true; }
inline void pipeWake( PipeFd fd ) { ::shutdown( fd, SHUT_RDWR ); }

inline bool pipeReadable( PipeFd fd, int ms )
{
    pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    return ::poll( &pfd, 1, ms ) > 0;
}

#endif

} // namespace atui_test

#endif // ATUI_TESTS_PIPE_SOCK_H
