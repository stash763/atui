// What a POSIX process does when it writes to a socket the peer has already closed.
//
// The hand-rolled transport sends with plain `send(..., 0)`.  On a POSIX socket that
// raises SIGPIPE, whose default disposition is to terminate the process -- so a
// server that hangs up at the wrong moment would not be an error message in atui, it
// would be atui gone.  curl was managing this (see the comment in
// `oai_client.cpp`), and `SIG_IGN` at startup is not available as an answer here:
// an ignored SIGPIPE is inherited by every command atui runs through the shell.
//
// Run it three ways and compare:
//
//   probe_sigpipe none      -> dies of SIGPIPE (exit 141 / "Broken pipe: 13")
//   probe_sigpipe msgflag   -> MSG_NOSIGNAL: Linux only, does not compile elsewhere
//   probe_sigpipe sockopt   -> SO_NOSIGPIPE on the socket: BSD and macOS
//
// Host-only probe; there is no SIGPIPE on OS/2 or Windows.  Build:
//   c++ -std=c++98 -o /tmp/probe_sigpipe tools/probe_sigpipe.cpp
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

enum Mode { MNone, MMsgFlag, MSockOpt };

int main( int argc, char **argv )
{
    Mode mode = MNone;
    if( argc > 1 )
    {
        if( std::strcmp( argv[1], "msgflag" ) == 0 ) mode = MMsgFlag;
        else if( std::strcmp( argv[1], "sockopt" ) == 0 ) mode = MSockOpt;
    }

    int ls = ::socket( AF_INET, SOCK_STREAM, 0 );
    int one = 1;
    ::setsockopt( ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one );
    sockaddr_in addr;
    std::memset( &addr, 0, sizeof addr );
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    addr.sin_port = 0;
    if( ::bind( ls, (sockaddr *) &addr, sizeof addr ) != 0 ) return 1;
    socklen_t len = sizeof addr;
    if( ::getsockname( ls, (sockaddr *) &addr, &len ) != 0 ) return 2;
    const int port = ntohs( addr.sin_port );
    if( ::listen( ls, 1 ) != 0 ) return 3;

    // The peer: accept the connection and close it without reading or answering.
    if( ::fork() == 0 )
    {
        const int c = ::accept( ls, 0, 0 );
        ::close( c );
        ::_exit( 0 );
    }

    const int s = ::socket( AF_INET, SOCK_STREAM, 0 );
    sockaddr_in peer = addr;
    peer.sin_port = htons( (unsigned short) port );
    if( ::connect( s, (sockaddr *) &peer, sizeof peer ) != 0 ) return 4;

    if( mode == MSockOpt )
    {
        int on = 1;
        if( ::setsockopt( s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on ) != 0 )
            std::printf( "SO_NOSIGPIPE not available (errno %d)\n", errno );
    }

    // Learn about the hangup first: read until the stack reports the peer gone, so
    // the write below is not racing the FIN.  Without this the outcome depends on
    // which arrives first, and the probe reads as flaky when it is showing a real
    // disposition.
    {
        char drain[ 64 ];
        for( int i = 0; i < 20; ++i )
        {
            pollfd pfd;
            pfd.fd = s;
            pfd.events = POLLIN;
            if( ::poll( &pfd, 1, 100 ) <= 0 )
                break;
            const ssize_t n = ::recv( s, drain, sizeof drain, 0 );
            if( n <= 0 )
                break;                       // closed or reset: the state we want
        }
    }

    static char buf[ 65536 ];
    std::memset( buf, 'x', sizeof buf );
    for( int i = 0; i < 4; ++i )
    {
#ifdef MSG_NOSIGNAL
        const int flags = ( mode == MMsgFlag ) ? MSG_NOSIGNAL : 0;
#else
        const int flags = 0;
#endif
        const ssize_t n = ::send( s, buf, sizeof buf, flags );
        std::printf( "send %d -> %zd (errno %d: %s)\n",
                     i, n, errno, n < 0 ? std::strerror( errno ) : "ok" );
        std::fflush( stdout );
        ::usleep( 200000 );
    }
    std::printf( "SURVIVED\n" );
    return 0;
}
