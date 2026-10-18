/* Socket probe: can a Watcom program open a TCP connection on this box?

   atui needs one outstaying answer -- there is no libcurl for Open Watcom on
   OS/2, so the network client is either hand-rolled over BSD sockets or it does
   not exist.  This connects to the LAN inference endpoint and performs a plain
   HTTP/1.1 request with no TLS, which is all the port needs (atui is documented
   as local-network HTTP only).

   Watcom ships the BSD headers (h\os2\sys\socket.h, netinet\in.h) and the
   import library tcpip32.lib, so nothing from the IBM toolkit is required.

   Build:

     wcc386 -bt=os2 -i=d:\watcom\h\os2 -fo=probe_sock.obj probe_sock.c
     wlink system os2v2 name probe_sock.exe file probe_sock.obj lib d:\watcom\lib386\os2\tcpip32.lib
*/

#include <stdio.h>
#include <string.h>
#include <errno.h>

/* Watcom's OS/2 <sys/socket.h> was written against the IBM TCP/IP toolkit's
   type set: it names u_char, u_short, u_int, u_long and caddr_t without
   defining them, and it includes <sys/cdefs.h> at line 298 -- after the
   structures that need what is in it.  It pulls in D:\WATCOM\h\os2\types.h by
   itself, which supplies long_int and MAXSOCKETS but not one of the u_* names,
   so those five are declared below.

   The order below is the measurement, not a preference (sktest.cmd runs the
   five permutations).  <unistd.h> drags in a header that sets the guard
   os2\types.h tests, so with it first, os2\types.h skips itself silently and
   <sys/socket.h> then cannot see long_int or MAXSOCKETS; <sys/types.h> is not
   included at all, because the one that resolves from H\sys is a different
   file carrying none of the BSD names.  The error you get for getting this
   wrong is "missing or misspelled data type near long_int" from inside the
   compiler's own header, several files away from the include that caused it.

   Five typedefs is deliberately not D:\TOOLKIT\h on the include path: IBM's
   headers carry IBM-only pragmas, its sys\socket.h is a different file with a
   different fd_set, and a build that needs Watcom plus five lines goes together
   on a machine where nobody installed the development toolkit.

   Which import library: Watcom's own lib386\os2\tcpip32.lib, measured.  The
   toolkit's tcpip32.lib, tcpipdll.lib and tcp32dll.lib were never needed, and
   this probe linked against Watcom's on the first attempt and spoke HTTP to the
   inference endpoint: HTTP/1.1 200 OK, Server: llama.cpp.  */
#include <types.h>

typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned int   u_int;
typedef unsigned long  u_long;
typedef char          *caddr_t;

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>   /* for close(); after the socket headers, see above */

#define HOST "10.196.88.170"
#define PORT 10001

int main( void )
{
    struct sockaddr_in addr;
    char buf[512];
    int fd, n;
    const char *request =
        "GET /v1/models HTTP/1.1\r\n"
        "Host: " HOST ":" "10001\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n";

    printf( "== socket probe ==\n" );
    /* Labelled struct in_addr, not in_addr_t: this library has no in_addr_t,
       which is the sort of thing a printf label gets wrong for a week. */
    printf( "  sizeof(fd_set)=%d sizeof(struct sockaddr_in)=%d sizeof(struct in_addr)=%d\n",
            (int)sizeof( fd_set ), (int)sizeof( struct sockaddr_in ),
            (int)sizeof( struct in_addr ) );

    fd = socket( AF_INET, SOCK_STREAM, 0 );
    printf( "  socket -> %d (errno=%d)\n", fd, errno );
    if( fd < 0 )
        return 1;

    memset( &addr, 0, sizeof( addr ) );
    addr.sin_family = AF_INET;
    addr.sin_port = htons( PORT );
    addr.sin_addr.s_addr = inet_addr( HOST );
    printf( "  inet_addr(%s) = 0x%lx  htons(%d) = %d\n",
            HOST, (unsigned long)addr.sin_addr.s_addr, PORT, (int)htons( PORT ) );

    n = connect( fd, (struct sockaddr *)&addr, (int)sizeof( addr ) );
    printf( "  connect -> %d (errno=%d)\n", n, errno );
    if( n != 0 ) {
        close( fd );
        return 1;
    }

    n = send( fd, request, (int)strlen( request ), 0 );
    printf( "  send -> %d bytes (errno=%d)\n", n, errno );

    n = recv( fd, buf, sizeof( buf ) - 1, 0 );
    if( n > 0 ) {
        buf[n] = '\0';
        printf( "  recv -> %d bytes, first line: %s\n", n, buf );
        /* The response starts with "HTTP/1.1 200"; if that is what comes back,
           a hand-rolled client can talk to the endpoint and the port has a
           future. */
        printf( "  verdict: %s\n",
                strncmp( buf, "HTTP/1.1 200", 12 ) == 0 || strncmp( buf, "HTTP/1.0 200", 12 ) == 0
                    ? "HTTP works over Watcom sockets"
                    : "connected but the response is not what was expected" );
    } else {
        printf( "  recv -> %d (errno=%d)\n", n, errno );
        close( fd );
        return 1;
    }

    /* Name resolution is a separate question from routing: no DNS would mean
       atui's endpoint has to be given as a literal address on this target. */
    {
        struct hostent *he = gethostbyname( "localhost" );
        printf( "  gethostbyname(localhost) -> %s\n", he ? "resolved" : "FAILED" );
    }

    close( fd );
    return 0;
}
