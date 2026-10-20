/* probe_close.cpp -- how a socket is closed here, and what "not yet" is called.
 *
 * Development probe, not part of the program:
 *
 *     build.cmd netlink probe_close.cpp probe_close.exe
 *
 * Two things probe_nb.cpp settled, and one it could not.
 *
 * Settled: `connect()` on a non-blocking socket returns -1 with nothing useful in
 * errno, while select()-for-writing followed by getsockopt(SO_ERROR) says truly
 * that the connection came up; and `close()` -- the C library's close, on a
 * descriptor -- returns -1 for a socket handle.  So a client that closed its
 * sockets the obvious way leaked one per request, which for a program that opens
 * a connection per chat turn is how you arrive at "cannot open socket" on a
 * Tuesday afternoon with no idea why.
 *
 * Could not: the number for "nothing to read yet".  errno is not maintained for
 * socket calls by this stack, and `<errno.h>` has none of the BSD socket errors,
 * so a state machine that has to tell "not yet" from "the peer is finished" needs
 * the stack's own error number.  Reading the import library (`strings` over
 * D:\WATCOM\LIB386\OS2\tcpip32.lib) answers where to look, because the library
 * exports more than the headers declare:
 *
 *     soclose            the close that works on a socket handle
 *     sock_errno         the stack's own errno
 *     sock_strerror      and its words
 *     sock_init          what the IBM-shaped stacks want before any of the rest
 *     inet_ntop          which the headers lost but the library has
 *     send_file          which atui does not need and is interesting anyway
 *
 * So this file declares the four it needs -- redeclaring what a header already
 * declares is fine provided the signature matches, and if one does not, that is
 * itself the answer -- and asks: does `soclose` return 0 where `close` returned
 * -1; does a closed handle get reused (the leak test, since a handle that is
 * never reused and always grows is the leak made visible); what number does
 * `sock_errno` give for a read that would block, and what for a connection
 * refused; and does `sock_strerror` put it into words.
 */
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

#define HOST "10.196.88.170"
#define PORT 10001
#define DEAD_PORT 9            /* discard: on a host with nothing listening, refused */

/* The four the headers lost. Written as extern "C" because the library is C, and
   written here rather than in a header because until this file runs, they are
   hypotheses about an import library's name list. */
extern "C" {
    int  soclose( int );
    int  sock_errno( void );
    int  sock_init( void );
    char *sock_strerror( int );
}

static void one( const char *what, int value )
{
    printf( "  %-38s -> %6d   sock_errno=%d\n", what, value, sock_errno() );
}

/* The leak test, asked directly: six handles, closed with what the library says
   to use, and are the numbers coming back around? */
static void test_close( void )
{
    printf( "\n-- close, and whether handles come back --\n" );
    printf( "  sock_init()                              -> %6d\n", sock_init() );

    int h[ 6 ];
    for( int i = 0; i < 6; ++i )
    {
        h[i] = socket( AF_INET, SOCK_STREAM, 0 );
        printf( "  socket #%d                                -> %6d\n", i, h[i] );
    }

    /* The first three closed with the C library's close(), the rest with
       soclose(), because "close returns -1" was one observation and it is worth
       knowing whether it is every time. */
    errno = 0;
    for( int i = 0; i < 3; ++i )
        one( "close(h)", close( h[i] ) );
    for( int i = 3; i < 6; ++i )
        one( "soclose(h)", soclose( h[i] ) );

    for( int i = 0; i < 6; ++i )
    {
        int n = socket( AF_INET, SOCK_STREAM, 0 );
        printf( "  socket after closing (was %d)             -> %6d%s\n", h[i], n,
                n == h[i] ? "   (reused)" : "" );
        soclose( n );
    }
}

/* The number that decides whether a request is slow or hung. */
static void test_would_block( void )
{
    printf( "\n-- the stack's own error numbers --\n" );
    struct sockaddr_in sa;
    memset( &sa, 0, sizeof sa );
    sa.sin_family = AF_INET;
    sa.sin_port = htons( PORT );
    sa.sin_addr.s_addr = inet_addr( HOST );

    int s = socket( AF_INET, SOCK_STREAM, 0 );
    u_long on = 1;
    ioctl( s, FIONBIO, (char *) &on );
    connect( s, (struct sockaddr *) &sa, sizeof sa );

    fd_set w;
    FD_ZERO( &w );
    FD_SET( s, &w );
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    select( s + 1, 0, &w, 0, &tv );

    char buf[ 64 ];
    sock_errno();                       /* clear, so the next one is this call's */
    int n = recv( s, buf, sizeof buf, 0 );
    int e = sock_errno();
    printf( "  recv with nothing to read                  -> %6d   sock_errno=%d\n",
            n, e );
    printf( "  sock_strerror(%d)                           -> %s\n", e,
            sock_strerror( e ) );
    printf( "  (this is the number the transport compares against; 10035 is what a\n"
            "   Winsock-shaped stack calls EWOULDBLOCK)\n" );
    soclose( s );

    /* And a failure worth naming in an error message rather than an errno: port 9
       on the endpoint host, where nothing is listening for this. */
    s = socket( AF_INET, SOCK_STREAM, 0 );
    sa.sin_port = htons( DEAD_PORT );
    sock_errno();
    n = connect( s, (struct sockaddr *) &sa, sizeof sa );
    e = sock_errno();
    printf( "  connect to %d (nothing there)               -> %6d   sock_errno=%d: %s\n",
            DEAD_PORT, n, e, sock_strerror( e ) );
    soclose( s );
}

int main( void )
{
    setbuf( stdout, 0 );
    printf( "probe_close: soclose, sock_errno, sock_init\n" );
    test_close();
    test_would_block();
    printf( "\ndone\n" );
    return 0;
}
