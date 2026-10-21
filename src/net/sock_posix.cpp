// The BSD sockets path: Linux, FreeBSD, macOS.
//
// Of the three platforms this layer covers, this is the one that has nothing to
// compensate for -- descriptors are descriptors, `errno` is maintained, and
// `EINPROGRESS` means what the manual says it means.  It exists as its own file
// because the other two are not variations on it: OS/2 has no `O_NONBLOCK`, does
// not maintain `errno` for socket calls and does not close a socket with `close()`,
// and Windows wants a library started up before anything works.  Keeping them in
// separate files is what keeps those facts out of the state machine that uses this.
#include "sock.h"

#if !defined(_WIN32) && !defined(__OS2__)

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace atui {
namespace net {

namespace {

// A short line of its own each time: the failure this layer is written against is
// one where a socket call "succeeded" and did nothing, so what a call returned has
// to be recoverable after the fact from a log rather than guessed at later.
std::string why( const std::string &what, int code )
{
    std::string out( what );
    out += ": ";
    out += strerror( code );
    return out;
}

int toMillis( int ms )
{
    return ms < 0 ? 0 : ms;
}

} // anonymous namespace

void sockGlobalInit()
{
    // Nothing to start: the resolver and the stack are there, and there is no
    // reference count to get wrong.
}

bool sockStackReady( std::string *reason )
{
    (void) reason;
    return true;
}

std::string sockNumericAddress( const std::string &host )
{
    if( host.empty() )
        return std::string();

    // Already numeric means no resolver is consulted, which is the common case for
    // a LAN endpoint and the reason a settings file can name an address and get an
    // answer when DNS is down.
    in_addr direct;
    if( inet_aton( host.c_str(), &direct ) == 1 )
        return host;

    struct hostent *he = gethostbyname( host.c_str() );
    if( he == 0 || he->h_addr_list == 0 || he->h_addr_list[0] == 0 )
        return std::string();

    char text[ INET_ADDRSTRLEN ];
    if( inet_ntop( AF_INET, he->h_addr_list[0], text, sizeof text ) == 0 )
        return std::string();
    return std::string( text );
}

SockHandle sockConnectStart( const std::string &host, int port, std::string *reason )
{
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
            *reason = why( "cannot open a socket", errno );
        return kNoSocket;
    }

    // Writing to a socket whose peer has gone raises SIGPIPE, and the default
    // disposition of that signal is to terminate the process.  Measured on the host
    // (tools/probe_sigpipe.cpp): a peer that accepts, hangs up, and is written to
    // twice killed the process in 6 runs out of 6, with nothing printed -- so a
    // server that dropped a chat request would not have shown an error, it would
    // have shown an atui that had vanished.  Ignoring the signal process-wide is not
    // an answer here: that disposition is inherited by every command atui runs
    // through the shell, which is how `yes | head` starts misbehaving.  So it is
    // switched off on this socket, and only this one.  Both spellings are used
    // because the platforms disagree: `SO_NOSIGPIPE` is BSD and macOS (and Linux
    // 2.6+), `MSG_NOSIGNAL` is a per-send flag that Linux has and the BSDs do not.
    // Neither exists on OS/2 or Windows, which have no SIGPIPE.
#ifdef SO_NOSIGPIPE
    {
        int nosigpipe = 1;
        setsockopt( fd, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof nosigpipe );
    }
#endif

    // Non-blocking before connecting, so there is no window in which a connect to
    // a machine that is up-but-unanswered can block the screen.
    const int flags = fcntl( fd, F_GETFL, 0 );
    if( flags < 0 || fcntl( fd, F_SETFL, flags | O_NONBLOCK ) < 0 )
    {
        if( reason )
            *reason = why( "cannot make the connection non-blocking", errno );
        ::close( fd );
        return kNoSocket;
    }

    sockaddr_in sa;
    memset( &sa, 0, sizeof sa );
    sa.sin_family = AF_INET;
    sa.sin_port = htons( (unsigned short) port );
    if( inet_pton( AF_INET, numeric.c_str(), &sa.sin_addr ) != 1 )
    {
        if( reason )
            *reason = "cannot use the address " + numeric;
        ::close( fd );
        return kNoSocket;
    }

    // EINPROGRESS is the expected answer on a network this fast only when the
    // handshake has not finished, which is most of the time; everything else about
    // the attempt is read from SO_ERROR once select() says writable.
    if( connect( fd, (struct sockaddr *) &sa, sizeof sa ) != 0 )
    {
        if( errno != EINPROGRESS && errno != EWOULDBLOCK && errno != EINTR )
        {
            if( reason )
                *reason = why( "cannot connect", errno );
            ::close( fd );
            return kNoSocket;
        }
    }
    return fd;
}

SockWhy sockWait( SockHandle handle, bool forRead, int ms, std::string *reason )
{
    fd_set set;
    FD_ZERO( &set );
    FD_SET( handle, &set );

    // select() reports its remaining time back on some systems and not on others,
    // so the deadline is kept here and the timeout rebuilt for every restart after
    // a signal.
    struct timeval tv;
    tv.tv_sec = toMillis( ms ) / 1000;
    tv.tv_usec = ( toMillis( ms ) % 1000 ) * 1000;

    const int ready = select( (int) handle + 1, forRead ? &set : 0,
                              forRead ? 0 : &set, 0, &tv );
    if( ready == 0 )
        return srTimeout;
    if( ready < 0 )
    {
        if( errno == EINTR )
            return srAgain;
        if( reason )
            *reason = why( "cannot wait for the connection", errno );
        return srError;
    }
    return srOk;
}

SockWhy sockSend( SockHandle handle, const char *data, size_t len,
                  size_t *written, std::string *reason )
{
    if( written )
        *written = 0;
    // MSG_NOSIGNAL is the other half of the pair explained in sockConnectStart:
    // Linux's way of saying the same thing per call, for the sockets where the
    // socket option is not available.  Where the macro does not exist the option
    // above is what does the work.
#ifdef MSG_NOSIGNAL
    const ssize_t n = send( (int) handle, data, len, MSG_NOSIGNAL );
#else
    const ssize_t n = send( (int) handle, data, len, 0 );
#endif
    if( n > 0 )
    {
        if( written )
            *written = (size_t) n;
        return srOk;
    }
    if( n < 0 && ( errno == EAGAIN || errno == EWOULDBLOCK ) )
        return srAgain;
    if( n < 0 && errno == EINTR )
        return srAgain;
    if( reason )
        *reason = why( "the connection could not send", errno );
    return srError;
}

SockWhy sockRecv( SockHandle handle, char *buf, size_t cap,
                  size_t *got, std::string *reason )
{
    if( got )
        *got = 0;
    const ssize_t n = recv( (int) handle, buf, cap, 0 );
    if( n > 0 )
    {
        if( got )
            *got = (size_t) n;
        return srOk;
    }
    if( n == 0 )
        return srClosed;              // the peer is finished
    if( errno == EAGAIN || errno == EWOULDBLOCK )
        return srAgain;
    if( errno == EINTR )
        return srAgain;
    if( reason )
        *reason = why( "the connection could not read", errno );
    return srError;
}

std::string sockError( SockHandle handle )
{
    int err = 0;
    socklen_t len = sizeof err;
    if( getsockopt( (int) handle, SOL_SOCKET, SO_ERROR, &err, &len ) != 0 )
        return std::string();
    if( err == 0 )
        return std::string();
    return strerror( err );
}

void sockClose( SockHandle handle )
{
    if( handle == kNoSocket )
        return;
    ::close( (int) handle );
}

} // namespace atui::net
} // namespace atui

#endif // !_WIN32 && !__OS2__
