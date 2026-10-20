/* probe_net.cpp -- what does a BSD socket look like from C++ on this box?

   Development probe, not part of the program:

       build.cmd netlink probe_net.cpp probe_net.exe

   and run it, and read what it prints (stdout is fine over telnet; the run
   takes a couple of seconds, most of it waiting for the endpoint).

   atui has to speak HTTP/1.1 to a plain-HTTP inference endpoint with no libcurl,
   inside a TUI that must stay responsive: no worker thread on this target, so a
   request is a state machine pumped by the event loop's timer -- connect, write,
   read -- where each step is attempted without waiting and retried on the next
   tick.  That design stands or falls on five questions that a header does not
   answer:

     1. can a socket be made non-blocking here, and with which call -- the BSD
        `ioctl(FIONBIO)`, since this library's <fcntl.h> has no O_NONBLOCK;
     2. what does a connect() in progress report, and does select() for writing
        say when it ended (and getsockopt(SO_ERROR) say how it ended);
     3. what does a read report when there is nothing yet -- and, one level apart,
        when the peer has finished, because "not yet" and "no more" being
        indistinguishable is a hung request instead of a slow one;
     4. whether a host name resolves at all, since a provider is configured with
        a URL and people write host names;
     5. how large a write one send() takes, because a chat request with a system
        prompt behind it is comfortably more than a buffer.

   It talks to the endpoint the rest of the notes use -- 10.196.88.170:10001, a
   llama.cpp server on the LAN, HTTP 200 measured -- and prints what it sees
   rather than a verdict, because the numbers are the point.

   The include order is the one probe_sock.c proved by working: plain C library
   headers first, then <types.h>, then the five typedefs <sys/socket.h> expects
   the IBM toolkit to have supplied, then the socket headers, then <unistd.h>
   last.  It is not a style.  Getting it wrong fails inside the compiler's own
   headers with "missing or misspelled data type near long_int", several files
   from the include that caused it.

   Two things this file deliberately does not use, because the first compile said
   they are not there: the Windows spellings SOCKET / INVALID_SOCKET /
   closesocket (a socket is an int and it is closed with close()), and fcntl()
   with O_NONBLOCK (this <fcntl.h> is the C RTL's file-control header and has no
   such macro).  What it uses instead is <sys/ioctl.h>'s ioctl() with FIONBIO.
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

static void show_errno( const char *what )
{
    printf( "%-30s errno=%d (%s)\n", what, errno, strerror( errno ) );
}

static void probe_types( void )
{
    printf( "\n-- types and constants --\n" );
    printf( "sizeof(int)=%d sizeof(fd_set)=%d sizeof(sockaddr_in)=%d sizeof(timeval)=%d\n",
            (int) sizeof( int ), (int) sizeof( fd_set ),
            (int) sizeof( struct sockaddr_in ), (int) sizeof( struct timeval ) );
    printf( "FD_SETSIZE=%d  FIONBIO=0x%lx\n", FD_SETSIZE, (unsigned long) FIONBIO );
    printf( "EAGAIN=%d EINTR=%d EBADF=%d  <-- and that is all: this <errno.h> has\n",
            EAGAIN, EINTR, EBADF );
    printf( "        no BSD socket errors at all, so the values below are read off\n"
            "        the wire rather than named.  Whatever a socket reports is\n"
            "        printed as a number, and atui has to name them itself.\n" );

    int s = socket( AF_INET, SOCK_STREAM, 0 );
    printf( "socket() -> %d  (-1 is the failure; is a handle, not a small index)\n", s );
    close( s );
}

/* Question 4.  atui's providers are configured with a URL, and a user writes the
   host name as readily as the address. */
static void probe_dns( void )
{
    printf( "\n-- name resolution --\n" );
    const char *names[] = { "localhost", HOST, "atui.invalid.host" };
    for( int i = 0; i < 3; ++i )
    {
        errno = 0;
        struct hostent *he = gethostbyname( names[i] );
        if( !he )
        {
            printf( "gethostbyname(%-18s) -> none, ", names[i] );
            show_errno( "" );
            continue;
        }
        /* inet_ntoa, not inet_ntop: the latter is a newer invention, and this
           netdb does not have it. */
        char buf[ 64 ];
        strncpy( buf, inet_ntoa( *(struct in_addr *) he->h_addr_list[ 0 ] ),
                 sizeof buf - 1 );
        buf[ sizeof buf - 1 ] = 0;
        printf( "gethostbyname(%-18s) -> %s (addrtype=%d len=%d, %d address%s)\n",
                names[i], buf, he->h_addrtype, he->h_length,
                he->h_addr_list[ 1 ] ? 2 : 1, he->h_addr_list[ 1 ] ? "es" : "" );
    }
}

static void endpoint( struct sockaddr_in &sa )
{
    memset( &sa, 0, sizeof sa );
    sa.sin_family = AF_INET;
    sa.sin_port = htons( PORT );
    sa.sin_addr.s_addr = inet_addr( HOST );
}

/* A real streaming completion, kept tiny so the answer is quick.  The point is
   not the model's output but the shape of the bytes: a streaming response
   arrives in pieces over seconds, which is the case the event loop has to keep
   working through. */
static void probe_stream( void )
{
    printf( "\n-- a streaming request, blocking --\n" );
    struct sockaddr_in sa;
    endpoint( sa );
    int s = socket( AF_INET, SOCK_STREAM, 0 );
    if( s < 0 ) { show_errno( "socket" ); return; }
    if( connect( s, (struct sockaddr *) &sa, sizeof sa ) != 0 )
    {
        show_errno( "connect" );
        close( s );
        return;
    }
    const char *body =
        "{\"model\":\"x\",\"messages\":[{\"role\":\"user\",\"content\":\"say ok\"}],"
        "\"max_tokens\":4,\"stream\":true}";
    char head[ 512 ];
    int n = sprintf( head,
        "POST /v1/chat/completions HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Accept: text/event-stream\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n", HOST, PORT, (int) strlen( body ) );
    if( send( s, head, n, 0 ) != n )
        show_errno( "send(head)" );
    if( send( s, body, (int) strlen( body ), 0 ) != (int) strlen( body ) )
        show_errno( "send(body)" );

    char buf[ 4096 ];
    int pieces = 0, bytes = 0, showed = 0, ended = -1;
    for( ;; )
    {
        int r = recv( s, buf, sizeof buf - 1, 0 );
        if( r == 0 ) { ended = 0; break; }
        if( r < 0 ) { ended = -1; show_errno( "recv<0 ends the loop" ); break; }
        buf[r] = 0;
        bytes += r;
        ++pieces;
        if( showed < 2 )
        {
            printf( "recv -> %d: [%.60s]%s\n", r, buf, r > 60 ? "..." : "" );
            ++showed;
        }
        if( pieces > 2000 ) break;   /* enough for a shape */
    }
    printf( "read %d bytes in %d pieces; ended with recv %d (0 = the peer closed)\n",
            bytes, pieces, ended );
    close( s );
}

/* Question 5. */
static void probe_big_write( void )
{
    printf( "\n-- one big write --\n" );
    struct sockaddr_in sa;
    endpoint( sa );
    int s = socket( AF_INET, SOCK_STREAM, 0 );
    if( s < 0 || connect( s, (struct sockaddr *) &sa, sizeof sa ) != 0 )
    {
        show_errno( "connect" );
        if( s >= 0 ) close( s );
        return;
    }
    static char big[ 32 * 1024 ];
    memset( big, 'x', sizeof big );
    int n = send( s, big, sizeof big, 0 );
    printf( "send(32768) -> %d%s\n", n, n == (int) sizeof big ? "" : " (short!)" );
    show_errno( "send" );
    close( s );
}

/* Questions 1, 2 and 3. */
static void probe_nonblocking( void )
{
    printf( "\n-- non-blocking --\n" );
    struct sockaddr_in sa;
    endpoint( sa );

    int s = socket( AF_INET, SOCK_STREAM, 0 );
    if( s < 0 ) { show_errno( "socket" ); return; }

    u_long on = 1;
    errno = 0;
    int r = ioctl( s, FIONBIO, (char *) &on );
    printf( "ioctl(FIONBIO,1) -> %d, ", r );
    show_errno( "" );

    errno = 0;
    int c = connect( s, (struct sockaddr *) &sa, sizeof sa );
    printf( "connect, in progress -> %d, errno=%d (%s)%s\n", c, errno,
            strerror( errno ),
            c == 0 ? "  <-- completed immediately; the state machine must still "
                     "treat that as connected" : "" );

    fd_set w;
    FD_ZERO( &w );
    FD_SET( s, &w );
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    errno = 0;
    int sel = select( s + 1, 0, &w, 0, &tv );
    int err = 0;
    int len = sizeof err;
    getsockopt( s, SOL_SOCKET, SO_ERROR, (char *) &err, &len );
    printf( "select(write, s+1=%d) -> %d, SO_ERROR=%d, errno=%d (%s)\n",
            s + 1, sel, err, errno, strerror( errno ) );

    /* Nothing to read yet: the case the loop meets most, and it must not look
       like the end of the response. */
    char buf[ 256 ];
    fd_set rr;
    FD_ZERO( &rr );
    FD_SET( s, &rr );
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    errno = 0;
    sel = select( s + 1, &rr, 0, 0, &tv );
    printf( "select(read, idle 2s) -> %d, errno=%d (%s)\n", sel, errno,
            strerror( errno ) );
    errno = 0;
    int n = recv( s, buf, sizeof buf, 0 );
    printf( "recv on an idle socket -> %d, errno=%d (%s)\n", n, errno,
            strerror( errno ) );
    printf( "   (the number to note: it must be negative and not the one a closed\n"
            "    connection gives, and 10035 is what Winsock calls EWOULDBLOCK)\n" );

    /* Does select() mind being handed the handle + 1 as nfds, when a handle is
       5020 rather than 4?  If it does, the loop passes FD_SETSIZE or 0 instead,
       and this line says which. */
    printf( "note: handle is %d, so nfds=s+1 is %d\n", s, s + 1 );
    close( s );

    /* A whole response read non-blocking, ending in the peer's close: the same
       sequence the state machine walks, one piece at a time. */
    s = socket( AF_INET, SOCK_STREAM, 0 );
    if( s < 0 ) return;
    if( connect( s, (struct sockaddr *) &sa, sizeof sa ) != 0 )
    {
        show_errno( "connect (non-blocking read test)" );
        close( s );
        return;
    }
    ioctl( s, FIONBIO, (char *) &on );
    const char *req = "GET /models HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
    send( s, req, (int) strlen( req ), 0 );
    int total = 0, pieces = 0, wouldblock = 0, ended = -1;
    for( int loops = 0; loops < 20000; ++loops )
    {
        n = recv( s, buf, sizeof buf, 0 );
        if( n > 0 ) { total += n; ++pieces; continue; }
        if( n == 0 ) { ended = 0; break; }
        if( errno != 0 && ( ++wouldblock == 1 || wouldblock > 2000 ) )
        {
            printf( "  first would-block errno = %d (%s)\n", errno,
                    strerror( errno ) );
            if( wouldblock > 2000 ) break;   /* a server that has stopped talking */
        }
        if( wouldblock > 2000 ) break;
        continue;                            /* busy-wait: the loop would sleep */
        show_errno( "recv error" );
        break;
    }
    printf( "non-blocking read: %d bytes in %d pieces, %d would-block returns, "
            "ended with %d\n", total, pieces, wouldblock, ended );
    close( s );
}

int main( void )
{
    /* Unbuffered, because how this gets read is over a redirect into a file that
       is fetched while the program is still running -- and a block-buffered
       stdout means the file stays empty until the program exits, which looks
       exactly like a program that prints nothing.  (It is also why the other
       probes here write their own log files.) */
    setbuf( stdout, 0 );

    printf( "probe_net: BSD sockets from C++ on OS/2 (Watcom 1.9)\n" );
    probe_types();
    probe_dns();
    probe_stream();
    probe_big_write();
    probe_nonblocking();
    printf( "\ndone\n" );
    return 0;
}
