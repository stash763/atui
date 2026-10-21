// Can atui have threads on OS/2, and what do they cost?  Run on the target:
//
//     build.cmd thread probe_thread.cpp pthead.exe
//
// The question is not academic: the UI must not block while an HTTP response
// arrives or while a tool runs a command, and on this platform waiting for a child
// process has no non-blocking form at all, so "does not block" means "is not this
// thread".  The state machines (HttpStream, ChatRequest, ChatTurn) do not care who
// steps them -- see PORTING.md -- so what has to be known is whether a thread exists
// to step them on, and whether the things they touch work from one:
//
//   1. Does the Watcom C runtime start threads at all, and only with -mt?  (_beginthread)
//   2. Are they really concurrent, or is this a single-threaded runtime pretending?
//      (two children counting while the parent counts)
//   3. Does the C heap survive a free() from a child?  (a single-threaded heap shared
//      by two threads corrupts, and it corrupts later, somewhere else)
//   4. Does printf work from a child?  (stdio has its own lock or it has none)
//   5. THE ONE THAT DECIDES HTTP: does the TCP/IP stack let a secondary thread open a
//      socket and talk?  (sock_init + socket + connect + close from a child)
//   6. Is the kernel's own DosCreateThread usable as an alternative to the C runtime's?
//
// Written in C with printf, not in atui's C++ with the TUI: a probe that needs the
// library under test cannot tell the difference between its own bug and the
// platform's.
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>

#include <types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// tcpip32.lib exports these and no Watcom header declares them (measured, and
// written up in TESTING.md), so say so here rather than through src/net/sock.h --
// the probe has to compile even if that header does not.
// Declared by <sys/socket.h>: socket(), connect(), and the rest.  Repeating them
// here is E617 "cannot overload extern C functions" (measured), so only the three
// that no Watcom header declares at all are spelled out.
extern "C" {
    int  sock_init( void );
    int  sock_errno( void );
    // No soclose(): declared in atui's own sock_os2.cpp it links, and from this
    // probe, compiled -bm, the reference comes out `soclose_` and tcpip32.lib has
    // no such export.  Leaving the probe's socket open at exit costs one descriptor
    // in a program that then exits, and the question under test is whether the
    // stack answers a socket() and connect() from a secondary thread at all.
}

volatile unsigned long g_parent = 0;      // parent's count while children run
volatile unsigned long g_childA = 0;
volatile unsigned long g_childB = 0;
int                    g_sockA = -1;      // socket opened by child A
int                    g_sockA_errno = 0;
int                    g_connectA = -1;
int                    g_heapA = -1;      // bytes of a buffer freed by child A

void childA( void * /*arg*/ )
{
    // 3. heap: allocate, fill, free, and report the byte count so the parent can
    // tell "did nothing" from "did it and survived".
    char *buf = (char *) malloc( 4096 );
    if( buf ) {
        memset( buf, 'a', 4096 );
        g_heapA = (int) buf[ 17 ];
        free( buf );
    }

    // 5. sockets from a secondary thread.  Connect to a port on this machine that
    // will refuse (7, echo, is refused rather than filtered on a LAN box); the
    // point is not success but whether the stack answers the call at all: -1 with
    // 10061 (refused) means the stack works from here, -1 with something else is a
    // story about threads and stacks, and a hang would be the worst answer.
    g_sockA_errno = sock_init();
    g_sockA = socket( AF_INET, SOCK_STREAM, 0 );
    if( g_sockA >= 0 ) {
        struct sockaddr_in a;
        memset( &a, 0, sizeof a );
        a.sin_family = AF_INET;
        a.sin_port = htons( 7 );
        a.sin_addr.s_addr = inet_addr( "127.0.0.1" );
        g_connectA = connect( g_sockA, (struct sockaddr *) &a, sizeof a );
        if( g_connectA != 0 )
            g_connectA = -sock_errno();
    }

    // 2. concurrency: count while the parent counts.  If the children only run when
    // the parent sleeps, g_parent stays tiny; with real threads all three grow.
    for( unsigned long i = 0; i < 200000UL; ++i )
        g_childA = i;
}

void childB( void * /*arg*/ )
{
    for( unsigned long i = 0; i < 200000UL; ++i )
        g_childB = i;
    printf( "[probe] child B printf works\n" );
    fflush( stdout );
}

// 6. the kernel's own thread API, which needs no C runtime cooperation and so is
// the fallback if the multithreaded runtime turns out not to be usable with the
// libraries atui links.  Signature from the toolkit's h/bsedos.h:
//     typedef VOID APIENTRY FNTHREAD( ULONG );   typedef FNTHREAD *PFNTHREAD;
//     APIRET APIENTRY DosCreateThread( PTID, PFNTHREAD, ULONG param,
//                                      ULONG flag, ULONG cbStack );
// so the child takes a ULONG and returns VOID -- not a pointer argument, and not
// 42.  flag is not a priority: 0 is the default class.
VOID APIENTRY kernelChild( ULONG /*param*/ )
{
    printf( "[probe] DosCreateThread child runs\n" );
    fflush( stdout );
}

int main( int argc, char **argv )
{
    // Whether the compiler thinks this is a multithreaded build at all: the C
    // library headers test __MT__ to decide whether _beginthread and friends exist,
    // so print what the macro says rather than guessing at the switch.  -bm is what
    // sets it (-mt is not an option this compiler has: E068).
#ifdef __MT__
    printf( "[probe] __MT__ is defined as %d\n", __MT__ );
#else
    // Measured with -bm: this line is what prints.  `__MT__` is NOT set by -bm, and
    // yet _beginthread creates threads that run (see the counts at the end), so the
    // macro is not the thing to test for "can this build thread" -- -bm plus a run
    // is.  (Watcom's own headers test it, which is where the W869 warning came from
    // before -bm was on the command line.)
    printf( "[probe] __MT__ is NOT defined (and -bm still gives working threads)\n" );
#endif
    printf( "[probe] %s\n", argc > 0 ? argv[ 0 ] : "probe_thread" );

    unsigned long tidA = 0, tidB = 0;
    int hA = _beginthread( childA, 0, 64 * 1024, NULL );
    int hB = _beginthread( childB, 0, 64 * 1024, NULL );
    printf( "[probe] _beginthread -> %d and %d (0 means it refused)\n", hA, hB );

    ULONG ktid = 0;
    APIRET krc = DosCreateThread( (PTID) &ktid, (PFNTHREAD) kernelChild, 0UL, 0UL,
                                  32UL * 1024UL );
    printf( "[probe] DosCreateThread rc=%lu tid=%lu\n", (unsigned long) krc,
            (unsigned long) ktid );

    for( unsigned long i = 0; i < 200UL; ++i ) {
        DosSleep( 10 );
        ++g_parent;
    }

    printf( "[probe] parent=%lu childA=%lu childB=%lu (all three grown means real concurrency)\n",
            g_parent, g_childA, g_childB );
    printf( "[probe] child heap byte=%d  sock_init=%d socket=%d connect=%d (connect -10061 is refused: stack works)\n",
            g_heapA, g_sockA_errno, g_sockA, g_connectA );
    fflush( stdout );
    DosSleep( 200 );
    return 0;
}
