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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

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
    printf( "  sizeof(fd_set)=%d sizeof(struct sockaddr_in)=%d sizeof(in_addr_t)=%d\n",
            (int)sizeof( fd_set ), (int)sizeof( struct sockaddr_in ),
            (int)sizeof( in_addr_t ) );

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
