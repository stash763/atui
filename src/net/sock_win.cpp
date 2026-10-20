// Windows over Winsock2: the third spelling of the same four operations.
//
// NOT YET COMPILED.  Windows has no toolchain in this tree yet (there is no CI, and
// no cross compiler has been chosen -- see the platform coverage note in
// TESTING.md), so what follows is written against the API and not against a
// compiler.  That is worth saying plainly rather than letting the file look as
// settled as the two above it, which were each compiled and run before a line of
// them was written.  The state machine that uses this is exercised on the platforms
// where it can be, and when a Windows build exists this file's first job is to be
// wrong in a way the tests notice.
//
// The differences from the POSIX path that matter here: a socket is a `SOCKET`
// (pointer-sized) and not a descriptor, so it is closed with `closesocket()`; the
// error number is `WSAGetLastError()` and it is always maintained; non-blocking is
// `ioctlsocket(s, FIONBIO, &on)`; and nothing works at all until `WSAStartup()` has
// been called, which is reference counted and therefore has to be balanced by
// `WSACleanup()` at exit.
#include "sock.h"

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>    // the fallback message when FormatMessage has none
#include <string.h>

namespace atui {
namespace net {

namespace {

const SockHandle kInvalidAsHandle = (SockHandle) (UINT_PTR) INVALID_SOCKET;

std::string why( const std::string &what, int code )
{
    char *text = 0;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS;
    if( FormatMessageA( flags, 0, (DWORD) code, 0, (LPSTR) &text, 0, 0 ) == 0 ||
        text == 0 )
    {
        char fallback[ 40 ];
        sprintf( fallback, "socket error %d", code );
        return what + ": " + fallback;
    }
    std::string message( text );
    LocalFree( text );
    while( !message.empty() && ( message[ message.size() - 1 ] == '\n' ||
                                 message[ message.size() - 1 ] == '\r' ) )
        message.erase( message.size() - 1 );
    return what + ": " + message;
}

SOCKET asSocket( SockHandle handle )
{
    return (SOCKET) (UINT_PTR) handle;
}

} // anonymous namespace

void sockGlobalInit()
{
    WSADATA wsa;
    WSAStartup( MAKEWORD( 2, 2 ), &wsa );
}

bool sockStackReady( std::string *reason )
{
    WSADATA wsa;
    const int rc = WSAStartup( MAKEWORD( 2, 2 ), &wsa );
    if( rc != 0 )
    {
        if( reason )
            *reason = why( "no TCP/IP stack", rc );
        return false;
    }
    return true;
}

std::string sockNumericAddress( const std::string &host )
{
    if( host.empty() )
        return std::string();

    const unsigned long direct = inet_addr( host.c_str() );
    if( direct != INADDR_NONE )
        return host;

    hostent *he = gethostbyname( host.c_str() );
    if( he == 0 || he->h_addr_list == 0 || he->h_addr_list[0] == 0 )
        return std::string();
    return std::string( inet_ntoa( *(struct in_addr *) he->h_addr_list[0] ) );
}

SockHandle sockConnectStart( const std::string &host, int port, std::string *reason )
{
    sockGlobalInit();

    const std::string numeric = sockNumericAddress( host );
    if( numeric.empty() )
    {
        if( reason )
            *reason = "cannot find " + host;
        return kNoSocket;
    }

    SOCKET s = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if( s == INVALID_SOCKET )
    {
        if( reason )
            *reason = why( "cannot open a socket", WSAGetLastError() );
        return kNoSocket;
    }

    u_long on = 1;
    ioctlsocket( s, FIONBIO, &on );

    sockaddr_in sa;
    memset( &sa, 0, sizeof sa );
    sa.sin_family = AF_INET;
    sa.sin_port = htons( (u_short) port );
    sa.sin_addr.s_addr = inet_addr( numeric.c_str() );

    // A connection in progress is WSAEWOULDBLOCK; WSAEINVAL means it is still being
    // negotiated, which is the same answer for a caller that then waits.  Both are
    // success here, and SO_ERROR after select() is the verdict.
    if( connect( s, (struct sockaddr *) &sa, sizeof sa ) == SOCKET_ERROR )
    {
        const int err = WSAGetLastError();
        if( err != WSAEWOULDBLOCK && err != WSAEINVAL )
        {
            if( reason )
                *reason = why( "cannot connect", err );
            closesocket( s );
            return kNoSocket;
        }
    }
    return (SockHandle) (UINT_PTR) s;
}

SockWhy sockWait( SockHandle handle, bool forRead, int ms, std::string *reason )
{
    fd_set set;
    FD_ZERO( &set );
    FD_SET( asSocket( handle ), &set );

    timeval tv;
    tv.tv_sec = ms > 0 ? ms / 1000 : 0;
    tv.tv_usec = ms > 0 ? ( ms % 1000 ) * 1000 : 0;

    const int ready = select( 0, forRead ? &set : 0, forRead ? 0 : &set, 0, &tv );
    if( ready == 0 )
        return srTimeout;
    if( ready == SOCKET_ERROR )
    {
        if( reason )
            *reason = why( "cannot wait for the connection", WSAGetLastError() );
        return srError;
    }
    return srOk;
}

SockWhy sockSend( SockHandle handle, const char *data, size_t len,
                  size_t *written, std::string *reason )
{
    if( written )
        *written = 0;
    const int n = send( asSocket( handle ), data, (int) len, 0 );
    if( n > 0 )
    {
        if( written )
            *written = (size_t) n;
        return srOk;
    }
    if( n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK )
        return srAgain;
    if( reason )
        *reason = why( "the connection could not send", WSAGetLastError() );
    return srError;
}

SockWhy sockRecv( SockHandle handle, char *buf, size_t cap,
                  size_t *got, std::string *reason )
{
    if( got )
        *got = 0;
    const int n = recv( asSocket( handle ), buf, (int) cap, 0 );
    if( n > 0 )
    {
        if( got )
            *got = (size_t) n;
        return srOk;
    }
    if( n == 0 )
        return srClosed;
    if( n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK )
        return srAgain;
    if( reason )
        *reason = why( "the connection could not read", WSAGetLastError() );
    return srError;
}

std::string sockError( SockHandle handle )
{
    int err = 0;
    int len = sizeof err;
    if( getsockopt( asSocket( handle ), SOL_SOCKET, SO_ERROR, (char *) &err, &len )
        == SOCKET_ERROR )
        return std::string();
    if( err == 0 )
        return std::string();
    if( err == WSAECONNREFUSED )
        return "the server refused the connection";
    return why( "the connection failed", err );
}

void sockClose( SockHandle handle )
{
    if( handle == kNoSocket || handle == kInvalidAsHandle )
        return;
    closesocket( asSocket( handle ) );
}

} // namespace atui::net
} // namespace atui

#endif // _WIN32
