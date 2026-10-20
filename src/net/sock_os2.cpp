// OS/2 over IBM TCP/IP: the socket layer where none of the usual names work.
//
// Everything unusual in this file is a measurement, not a style choice, and the
// numbers in it were read off the target rather than out of a document -- see the
// "Building on the target" section of TESTING.md and `tools/os2/probe_close.cpp`.
// They are worth restating, because each one is a way this file could look correct
// and be wrong:
//
//   * a socket is an `int`, its failure is -1, and it is closed with `soclose()`.
//     `close()` returns -1 for a socket handle and leaves it open, which is a leak
//     rather than an error and therefore invisible until the stack runs out;
//   * `errno` is not maintained for socket calls, and `<errno.h>` has no BSD socket
//     errors at all (`EAGAIN` is 18, an OS/2 number).  The stack's own error comes
//     from `sock_errno()`, and its words from `sock_strerror()`.  Two of its values
//     are what this layer is built around: 10035 "Operation would block", and 10061
//     "Connection refused";
//   * a non-blocking `connect()` returns -1 with nothing in errno and succeeds:
//     select()-for-writing followed by `SO_ERROR` is what says whether it came up.
//     So the return value of connect() is not looked at here;
//   * `ioctl(s, FIONBIO, &on)` is how non-blocking is set.  `<fcntl.h>` has no
//     `O_NONBLOCK`, so `fcntl()` is not an option;
//   * the headers must be included in this order, and `<sys/types.h>` must not
//     appear at all: it resolves the plain C headers out of the wrong directory and
//     the errors come back pointing at nothing in this file.
//
// `sock_errno`/`sock_strerror`/`sock_init`/`soclose` are exported by
// `tcpip32.lib` and declared by none of Watcom's headers, so they are declared
// below.  The library is part of the link only for the programs that need it -- an
// import library records the stack in the image, so a program that never opened a
// socket would still require TCP/IP at load time.
#include "sock.h"

#ifdef __OS2__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <io.h>

#include <types.h>

typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned int   u_int;
typedef unsigned long  u_long;
typedef char          *caddr_t;

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <unistd.h>

extern "C" {
    int   sock_init( void );
    int   sock_errno( void );
    char *sock_strerror( int );
    int   soclose( int );
}

namespace atui {
namespace net {

namespace {

// The two numbers this layer cannot do without, because no header here names them.
// Both measured on the target: a non-blocking read with nothing to read returns -1
// with `sock_errno()` 10035, and a connect to a port with nothing listening fails
// with 10061.  They are the Winsock values, which is what an IBM-derived stack
// uses; a build that got either wrong would sit waiting for bytes that had already
// been refused, or report a finished response as a broken one.
const int kWouldBlock  = 10035;
const int kRefused     = 10061;

std::string why( const std::string &what, int code )
{
    std::string out( what );
    out += ": ";
    char *text = code ? sock_strerror( code ) : 0;
    out += text ? text : "(no error recorded)";
    return out;
}

void setNonBlocking( int fd, int on )
{
    u_long value = on ? 1 : 0;
    ioctl( fd, FIONBIO, (char *) &value );
}

} // anonymous namespace

void sockGlobalInit()
{
    // The IBM-shaped stacks want to be told before any of the rest is called.  It
    // is idempotent, so calling it on every entry point costs nothing and removes
    // the question of which caller got there first.
    sock_init();
}

bool sockStackReady( std::string *reason )
{
    const int rc = sock_init();
    if( rc == 0 )
        return true;
    if( reason )
        *reason = "TCP/IP is not running on this machine (sock_init returned "
                  + std::string( itoa( rc ) ) + ")";
    return false;
}

std::string sockNumericAddress( const std::string &host )
{
    if( host.empty() )
        return std::string();

    // A dotted quad is parsed without asking anybody.  `inet_addr` returns
    // INADDR_NONE for both a bad string and 255.255.255.255, and the second is not
    // an address anyone is putting in a settings file; `inet_pton`, which gets that
    // case right, is in the library but not in these headers.
    const unsigned long direct = inet_addr( host.c_str() );
    if( direct != (unsigned long) INADDR_NONE )
        return host;

    hostent *he = gethostbyname( host.c_str() );
    if( he == 0 || he->h_addr_list == 0 || he->h_addr_list[0] == 0 )
        return std::string();
    return std::string( inet_ntoa( *(struct in_addr *) he->h_addr_list[0] ) );
}

SockHandle sockConnectStart( const std::string &host, int port, std::string *reason )
{
    sock_init();

    const std::string numeric = sockNumericAddress( host );
    if( numeric.empty() )
    {
        if( reason )
            *reason = "cannot find " + host;
        return kNoSocket;
    }

    const int fd = socket( AF_INET, SOCK_STREAM, 0 );
    if( fd < 0 )
    {
        if( reason )
            *reason = why( "cannot open a socket", sock_errno() );
        return kNoSocket;
    }

    setNonBlocking( fd, 1 );

    sockaddr_in sa;
    memset( &sa, 0, sizeof sa );
    sa.sin_family = AF_INET;
    sa.sin_port = htons( (unsigned short) port );
    sa.sin_addr.s_addr = inet_addr( numeric.c_str() );

    // Deliberately unchecked: the measured behaviour of this stack is that a
    // connection under way returns -1 with no error recorded, and a probe proved
    // the connection had come up with select() and SO_ERROR afterwards.  Anything
    // that treated the return value as the answer would fail every connection.
    connect( fd, (struct sockaddr *) &sa, sizeof sa );
    return fd;
}

SockWhy sockWait( SockHandle handle, bool forRead, int ms, std::string *reason )
{
    fd_set set;
    FD_ZERO( &set );
    FD_SET( (int) handle, &set );

    struct timeval tv;
    tv.tv_sec = ms > 0 ? ms / 1000 : 0;
    tv.tv_usec = ms > 0 ? ( ms % 1000 ) * 1000 : 0;

    const int ready = select( (int) handle + 1, forRead ? &set : 0,
                              forRead ? 0 : &set, 0, &tv );
    if( ready == 0 )
        return srTimeout;
    if( ready < 0 )
    {
        if( reason )
            *reason = why( "cannot wait for the connection", sock_errno() );
        return srError;
    }
    return srOk;
}

SockWhy sockSend( SockHandle handle, const char *data, size_t len,
                  size_t *written, std::string *reason )
{
    if( written )
        *written = 0;
    const int n = send( (int) handle, data, (int) len, 0 );
    if( n > 0 )
    {
        if( written )
            *written = (size_t) n;
        return srOk;
    }
    if( n < 0 && sock_errno() == kWouldBlock )
        return srAgain;
    if( reason )
        *reason = why( "the connection could not send", sock_errno() );
    return srError;
}

SockWhy sockRecv( SockHandle handle, char *buf, size_t cap,
                  size_t *got, std::string *reason )
{
    if( got )
        *got = 0;
    const int n = recv( (int) handle, buf, (int) cap, 0 );
    if( n > 0 )
    {
        if( got )
            *got = (size_t) n;
        return srOk;
    }
    if( n == 0 )
        return srClosed;              // the peer is finished
    if( sock_errno() == kWouldBlock )
        return srAgain;
    if( reason )
        *reason = why( "the connection could not read", sock_errno() );
    return srError;
}

std::string sockError( SockHandle handle )
{
    int err = 0;
    int len = sizeof err;
    if( getsockopt( (int) handle, SOL_SOCKET, SO_ERROR, (char *) &err, &len ) != 0 )
        return std::string();
    if( err == 0 )
        return std::string();
    if( err == kRefused )
        return "the server refused the connection";
    char *text = sock_strerror( err );
    return text ? std::string( text ) : std::string( "the connection failed" );
}

void sockClose( SockHandle handle )
{
    if( handle == kNoSocket )
        return;
    soclose( (int) handle );
}

} // namespace atui::net
} // namespace atui

#endif // __OS2__
