/* probe_nb.cpp -- the non-blocking sequence, every return value printed.

   Development probe, not part of the program:

       build.cmd netlink probe_nb.cpp probe_nb.exe

   probe_net.cpp answered the shape of the API and left the behaviour open at
   exactly the point the event-loop transport depends on.  What it found:

     * ioctl(s, FIONBIO, &on) -> 0, and a socket is an int closed with close();
     * connect() on a non-blocking socket returned -1 with errno 0, while
       select() for writing then said ready and getsockopt(SO_ERROR) said 0 -- so
       the connection succeeded and the return value said nothing about it;
     * recv() with nothing to read returned -1 with errno 0, and recv() after the
       peer closed returned 0.  Those two are the pair the transport must tell
       apart, and -1/0 is enough to tell them;
     * errno is not maintained for socket calls by this stack: it read 0 after a
       -1 and 4 ("Bad file number", from an unrelated call) after a send() that
       returned the full 32768.  Nothing here may branch on errno;
     * one non-blocking read loop got nothing at all -- 0 bytes in 0 pieces over
       2001 attempts -- which is either the request never having been sent or the
       answer never arriving, and the send() return was not printed, so the probe
       could not say which.

   So this one prints every return value of every call, in the two orders a state
   machine can plausibly be written in (non-blocking before connect, and after),
   and asks whether the stack offers its own error number -- sock_errno(), which
   the IBM-derived stacks have.  That is guarded by USE_SOCK_ERRNO because whether
   the symbol exists is itself a measurement: flip the define below, rebuild, and
   either it links, and every error below gains a number, or wlink says it cannot
   find it, and atui is left with return values and SO_ERROR, which is an answer
   too.
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

#define HOST "10.196.88.170"   /* the llama.cpp endpoint: it answers a
#define PORT 10001              /* request, which is all that is wanted,
                                   and it costs no session slot on the
                                   box being measured */

/* Ask the box a question about itself: this is the one define to flip. */
/* #define USE_SOCK_ERRNO 1 */

#ifdef USE_SOCK_ERRNO
extern "C" int sock_errno( void );
static int why( void ) { return sock_errno(); }
#else
static int why( void ) { return 0; }   /* not asked, so not a number */
#endif

static int g_step = 0;

/* One line per call, numbered, because the order is part of the measurement. */
static void said( const char *what, int value, int err )
{
    printf( "%3d  %-34s -> %6d   sock=%d errno=%d\n", ++g_step, what, value,
            why(), err );
}

static void nb( void )
{
    struct sockaddr_in sa;
    memset( &sa, 0, sizeof sa );
    sa.sin_family = AF_INET;
    sa.sin_port = htons( PORT );
    sa.sin_addr.s_addr = inet_addr( HOST );

    printf( "\n-- non-blocking, then connect --\n" );
    int s = socket( AF_INET, SOCK_STREAM, 0 );
    said( "socket", s, errno );

    u_long on = 1;
    errno = 0;
    said( "ioctl(FIONBIO,1) before connect", ioctl( s, FIONBIO, (char *) &on ), errno );

    errno = 0;
    int c = connect( s, (struct sockaddr *) &sa, sizeof sa );
    printf( "%3d  %-34s -> %6d   sock=%d errno=%d%s\n", ++g_step, "connect", c,
            why(), errno,
            c == 0 ? "   (said yes outright)" : "   (said no: read the next two)" );

    /* The connect's outcome is select() plus SO_ERROR, whatever the return said. */
    fd_set w;
    FD_ZERO( &w );
    FD_SET( s, &w );
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    errno = 0;
    said( "select(write, 5s)", select( s + 1, 0, &w, 0, &tv ), errno );

    int err = 0, len = sizeof err;
    errno = 0;
    said( "getsockopt(SO_ERROR)",
          getsockopt( s, SOL_SOCKET, SO_ERROR, (char *) &err, &len ), errno );
    printf( "      SO_ERROR = %d%s\n", err, err == 0 ? "   (connected)" : "   (FAILED)" );

    /* A little request, with the send's return value printed: probe_net.cpp did
       not print it, and its read loop then saw nothing at all.  Telnet was chosen
       for the peer because its banner arrives unprompted, which makes "nothing to
       read" a state that is certain to occur and certain to be noticed. */
    const char *req = "GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
    errno = 0;
    said( "send(request)", send( s, req, (int) strlen( req ), 0 ), errno );

    /* Now read until the peer closes or this stops being interesting. */
    char buf[ 512 ];
    int pieces = 0, total = 0, minus = 0, zero = 0;
    for( int i = 0; i < 400; ++i )
    {
        fd_set r;
        FD_ZERO( &r );
        FD_SET( s, &r );
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int k = select( s + 1, &r, 0, 0, &tv );
        if( k == 0 )
        {
            printf( "      select timed out at read %d (nothing further)\n", pieces );
            break;
        }
        if( k < 0 ) { said( "select(read) error", k, errno ); break; }
        int n = recv( s, buf, sizeof buf, 0 );
        if( n > 0 )
        {
            if( pieces < 2 )
                printf( "      recv %d: [%.48s]%s\n", n, buf, n > 48 ? "..." : "" );
            ++pieces;
            total += n;
            continue;
        }
        if( n == 0 ) { ++zero; printf( "      recv -> 0 (peer closed)\n" ); break; }
        if( ++minus == 1 )
            printf( "      first -1: sock=%d errno=%d, readable was %d\n",
                    why(), errno, k );
    }
    printf( "      read %d bytes in %d pieces, %d recv of -1, %d of 0\n",
            total, pieces, minus, zero );
    errno = 0;
    said( "close", close( s ), errno );
}

/* The other order: connect in blocking mode (which is what a first attempt at a
   state machine often does -- make it non-blocking only once connected), and then
   the same read loop.  If the two orders behave differently, the transport has to
   know which one it is allowed to use. */
static void blocking_then_nonblocking( void )
{
    printf( "\n-- connect blocking, then non-blocking --\n" );
    struct sockaddr_in sa;
    memset( &sa, 0, sizeof sa );
    sa.sin_family = AF_INET;
    sa.sin_port = htons( PORT );
    sa.sin_addr.s_addr = inet_addr( HOST );

    int s = socket( AF_INET, SOCK_STREAM, 0 );
    said( "socket", s, errno );
    errno = 0;
    said( "connect (blocking)", connect( s, (struct sockaddr *) &sa, sizeof sa ), errno );
    u_long on = 1;
    errno = 0;
    said( "ioctl(FIONBIO,1) after connect", ioctl( s, FIONBIO, (char *) &on ), errno );

    /* Ask, then read: this peer volunteers nothing.  Asking after FIONBIO is the
       point -- a write on a just-connected non-blocking socket is the state the
       transport is in on its first tick after connecting. */
    const char *req = "GET /models HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
    errno = 0;
    said( "send(request) non-blocking", send( s, req, (int) strlen( req ), 0 ), errno );
    char buf[ 512 ];
    int total = 0, pieces = 0, minus = 0;
    for( int i = 0; i < 200; ++i )
    {
        fd_set r;
        FD_ZERO( &r );
        FD_SET( s, &r );
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        int k = select( s + 1, &r, 0, 0, &tv );
        if( k <= 0 ) { printf( "      select gave %d at read %d\n", k, pieces ); break; }
        int n = recv( s, buf, sizeof buf, 0 );
        if( n > 0 )
        {
            if( pieces == 0 )
                printf( "      first bytes: [%.*s]\n", n > 60 ? 60 : n, buf );
            ++pieces;
            total += n;
            continue;
        }
        if( n == 0 ) { printf( "      recv -> 0 (peer closed)\n" ); break; }
        ++minus;
        printf( "      recv -> -1 sock=%d errno=%d\n", why(), errno );
        break;
    }
    printf( "      read %d bytes in %d pieces, %d of -1\n", total, pieces, minus );
    close( s );
}

int main( void )
{
    setbuf( stdout, 0 );
    printf( "probe_nb: the non-blocking sequence, call by call\n" );
    printf( "sock_errno: %s\n",
#ifdef USE_SOCK_ERRNO
            "asked for (this build links it or does not exist)"
#else
            "not asked for: rebuild with USE_SOCK_ERRNO to find out whether it exists"
#endif
          );
    nb();
    blocking_then_nonblocking();
    printf( "\ndone\n" );
    return 0;
}
