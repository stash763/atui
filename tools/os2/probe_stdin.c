/* Capability probe: how, if at all, does keyboard input reach a program the
   telnet server spawned?

   This is the question the first version of this file asked, and it answered
   half of it.  Measured then, in a session started by the bridge:

     isatty(0)=1  isatty(1)=1
     getsockopt(0, SOL_SOCKET, SO_TYPE) fails        -- stdin is not a socket
     select(1, {fd 0}, 20 s) three times -> never readable, while bytes were sent

   So either the bytes never reach this process, or select() cannot wait on a
   console handle and the data sits in a queue that only a console read drains.
   Those answers want opposite designs: the first means a full-screen program
   cannot be driven over the relay at all; the second means it can, but input has
   to come from a blocking call, which for an event loop means a reader thread and
   no select()-based source.  Getting this wrong costs the input half of the port,
   so both are tested rather than reasoned about.

   What is tried, in an order chosen so a hang in one cannot hide the other:

     1. select() again, briefly, to confirm the blindness reproduces and was not
        the driver's timing.
     2. kbhit()/getch() polled -- Watcom's console input, how every DOS and OS/2
        text program of the era saw keystrokes without blocking.  If this fires, a
        UI can poll and no reader thread is needed.
     3. a plain blocking read(0, ...), the console read C stdio performs.  This
        may never return, so it is last, and its silence is the answer: the driver
        will have fed it bytes.

   The extended-key form is printed as numbers deliberately.  If a key arrives as
   0 or 224 followed by a scan code, that is the DOS-style encoding, and the
   reader has to be written against what this really delivers rather than against
   VT escapes assumed.

   Sleep is DosSleep: usleep and nanosleep are not in this C library (measured,
   and recorded in the capability map), so the portable-looking call is the one
   that does not link.

   Drive it from the other end, feeding bytes between the markers:

     wcc386 -bt=os2 -5 -od -w3 -fo=ps.obj probe_stdin.c
     wlink system os2v2 name ps.exe file ps.obj library os2386.lib library tcpip32.lib
*/

#define INCL_DOSPROCESS
#define INCL_DOSERRORS
#include <os2.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <io.h>
#include <conio.h>

#include <types.h>

typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned int   u_int;
typedef unsigned long  u_long;
typedef char          *caddr_t;

#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>

#ifndef SOL_SOCKET
#define SOL_SOCKET  0xffff
#endif
#ifndef SO_TYPE
#define SO_TYPE     0x1008
#endif

#define POLL_MS       200
#define POLL_ROUNDS   100        /* 20 seconds of polling */
#define SELECT_SECS   5L

static void show( const unsigned char *bytes, int count )
{
    int i = 0;
    for( i = 0; i < count && i < 32; ++i )
    {
        unsigned char c = bytes[ i ];
        if( c == 0x1B )      printf( "ESC " );
        else if( c == '\r' ) printf( "CR " );
        else if( c == '\n' ) printf( "LF " );
        else if( c >= 0x20 && c < 0x7F ) printf( "%c ", (char) c );
        else                 printf( "%02X ", (unsigned) c );
    }
    if( count > 32 )
        printf( "...(+%d)", count - 32 );
}

int main( void )
{
    unsigned char buf[ 64 ];
    int           hits = 0;
    int           round = 0;

    printf( "STDIN2-START isatty(0)=%d isatty(1)=%d", isatty( 0 ), isatty( 1 ) );
    {
        int type = -1;
        int len  = (int) sizeof( type );
        printf( " stdin_socket=%s",
                getsockopt( 0, SOL_SOCKET, SO_TYPE, (void *) &type, &len ) == 0
                    ? "yes" : "no" );
    }
    printf( "\n" );
    fflush( stdout );

    /* 1. Is select() still blind to fd 0?  One call, one answer. */
    {
        fd_set         readable;
        struct timeval timeout;
        int            rc = 0;

        FD_ZERO( &readable );
        FD_SET( 0, &readable );
        timeout.tv_sec  = SELECT_SECS;
        timeout.tv_usec = 0;
        rc = select( 1, &readable, NULL, NULL, &timeout );
        printf( "STDIN2-SELECT select(%ld s) -> %d%s errno=%d\n",
                SELECT_SECS, rc, rc == 0 ? " (timed out, nothing seen)" : "", errno );
    }
    fflush( stdout );

    /* 2. Polled console input.  Nothing here can block: kbhit() answers
       immediately, and getch() is only called when it says there is a key. */
    printf( "STDIN2-KBHIT-BEGIN polling for %d ms\n", POLL_ROUNDS * POLL_MS );
    fflush( stdout );
    for( round = 0; round < POLL_ROUNDS; ++round )
    {
        if( kbhit() )
        {
            int c = getch();
            printf( "  kbhit: first=%d", c );
            if( c == 0 || c == 224 )
            {
                /* The DOS extended-key prefix.  kbhit again rather than select:
                   select cannot see this handle, as step 1 just showed. */
                DosSleep( 200 );
                if( kbhit() )
                    printf( " then=%d", getch() );
                else
                    printf( " then=nothing (so not an extended key)" );
            }
            printf( "\n" );
            fflush( stdout );
            ++hits;
        }
        DosSleep( POLL_MS );
    }
    printf( "STDIN2-KBHIT-END hits=%d\n", hits );
    fflush( stdout );

    /* 3. The blocking console read, last because it may never return. */
    printf( "STDIN2-READ-BEGIN blocking read of up to 32 bytes\n" );
    fflush( stdout );
    memset( buf, 0, sizeof( buf ) );
    {
        int got = (int) read( 0, buf, sizeof( buf ) - 1 );
        if( got > 0 )
        {
            printf( "  read returned %d byte(s): ", got );
            show( buf, got );
            printf( "\n" );
        }
        else
            printf( "  read returned %d (errno=%d)\n", got, errno );
    }
    fflush( stdout );

    printf( "STDIN2-END kbhit_hits=%d\n", hits );
    printf( "  reading the result:\n" );
    printf( "    keys seen by kbhit, or a read that returns -> input reaches this\n" );
    printf( "    process and select() cannot wait on it: read on a blocking call.\n" );
    printf( "    nothing seen and a read that never returns -> no input reaches a\n" );
    printf( "    child process in this session, and the relay shape must change.\n" );
    return 0;
}
