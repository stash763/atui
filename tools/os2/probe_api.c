/* Capability probe: the OS/2 API surface the port builds its threads, waits and
   directory walking on.

   Two questions in one file, because both are answered by one link line.

   One: does select() exist, and what does it take to make struct timeval
   visible?  The network loop needs a read timeout, and without one a stalled
   connection holds the UI until the user gives up.

   Two: do the kernel primitives behave?  Taking the address of DosCreateThread
   and friends says the import library provides the entry points; the semaphore
   calls say they work, which is the part a link test cannot show.  If a mutex
   and an event semaphore work here, then std::mutex and a condition variable on
   top of them are bookkeeping.  If they did not, the answer would be a single
   threaded build with an explicit pump, and that is a decision worth measuring
   rather than discovering late.

   Results, measured on the target (eComStation, Watcom 1.9, os2386.lib):

     select() compiles once <sys/time.h> is included -- <sys/socket.h> does not
     give timeval, which matters because select() is its first real user -- and
     it links against tcpip32.lib, not against the C library, which is where
     probe_sym looked and found nothing.  On an unconnected socket with a one
     second timeout it returns 0 with errno 0, so the deadline mechanism a read
     loop needs is there.  (Selecting on stdin instead gives -1, which is what
     select() does to a descriptor that is not a socket; the first version of
     this probe did exactly that and appeared to report a failure.)

     DosCreateMutexSem 0, DosRequestMutexSem 0, DosReleaseMutexSem 0.
     DosCreateEventSem 0; DosWaitEventSem on an unposted event returns 640,
     ERROR_TIMEOUT, and returns 0 after DosPostEventSem.  Private (unnamed)
     semaphores, which is all an in-process mutex needs.

   What did NOT compile is recorded here too, because the next person will try:
   DosQuerySysInfo and QSV_MAX_PATH_LENGTH are behind an include guard this
   os2.h does not open with the defines in use, and the exit-thread entry point
   is neither DosExitThread nor Dos_ExitThread as spelled here.  Chasing those
   two over a telnet link is not worth the round trips; neither is needed for
   the threading decision.

   Build (Open Watcom, OS/2):

     wcc386 -bt=os2 -5 -od -w3 -fo=probe_api.obj probe_api.c
     wlink system os2v2 name probe_api.exe file probe_api.obj ^
           library tcpip32.lib library os2386.lib

   tcpip32.lib for the sockets, os2386.lib for the Dos* calls -- Watcom does not
   link either by default, which is exactly why the symbol survey reported the
   Dos* names as undefined and why that finding meant nothing about the platform.
*/

#define INCL_DOSSEMAPHORES
#define INCL_DOSPROCESS
#define INCL_DOSFILEMGR
#include <os2.h>

#include <stdio.h>
#include <errno.h>

/* The typedefs and the include order are the socket header's requirement, not
   a style: see the longer note in probe_sock.c.  os2\types.h must be reached
   before anything sets the guard it tests, <sys/types.h> must not be included
   at all, and <unistd.h> comes after the socket headers. */
#include <types.h>

typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned int   u_int;
typedef unsigned long  u_long;
typedef char          *caddr_t;

#include <sys/socket.h>
#include <sys/time.h>   /* struct timeval: socket.h does not provide it */
#include <netinet/in.h>
#include <unistd.h>

static volatile long g_sink;

/* Addresses only: this is a question about the import library, and a call would
   be a question about a signature that this probe has no business getting wrong.
   Actually starting a thread is the threading work itself, and will be tested
   where it can be observed, not from a survey. */
static void link_survey( void )
{
    printf( "== entry points the port needs from the kernel ==\n" );
    g_sink += (long)(void *) DosCreateThread;
    g_sink += (long)(void *) DosWaitThread;
    g_sink += (long)(void *) DosSleep;
    g_sink += (long)(void *) DosExecPgm;
    g_sink += (long)(void *) DosWaitChild;
    g_sink += (long)(void *) DosFindFirst;
    g_sink += (long)(void *) DosFindNext;
    g_sink += (long)(void *) DosQueryPathInfo;
    printf( "  threads, child processes, directory walking: all resolved\n" );
}

static void semaphore_behaviour( void )
{
    HMTX mtx = 0;
    HEV  evt = 0;
    ULONG rc;

    printf( "== do the primitives behave? ==\n" );

    rc = DosCreateMutexSem( 0, &mtx, 0, 0 );
    printf( "  DosCreateMutexSem   -> %u\n", rc );
    if( rc == 0 )
    {
        printf( "  DosRequestMutexSem  -> %u (0: the mutex was free)\n",
                DosRequestMutexSem( mtx, 0 ) );
        printf( "  DosReleaseMutexSem  -> %u\n", DosReleaseMutexSem( mtx ) );
        DosCloseMutexSem( mtx );
    }

    rc = DosCreateEventSem( 0, &evt, 0, FALSE );
    printf( "  DosCreateEventSem   -> %u\n", rc );
    if( rc == 0 )
    {
        printf( "  DosWaitEventSem     -> %u (640 ERROR_TIMEOUT: nothing posted)\n",
                DosWaitEventSem( evt, 100 ) );
        printf( "  DosPostEventSem     -> %u\n", DosPostEventSem( evt ) );
        printf( "  DosWaitEventSem     -> %u (0: the post was seen)\n",
                DosWaitEventSem( evt, 100 ) );
        DosCloseEventSem( evt );
    }
}

static void select_needs_a_timeout( void )
{
    fd_set rd;
    struct timeval tv;
    int fd, rc;

    printf( "== can a socket wait with a deadline? ==\n" );

    /* Not stdin: the first version selected on fd 0 and printed -1, which is
       what select() does to a descriptor that is not a socket and says nothing
       about timeouts.  A fresh socket with no peer is readable by nobody, so 0
       here means select() waited the whole second and gave up -- the behaviour
       the network loop depends on. */
    fd = socket( AF_INET, SOCK_STREAM, 0 );
    if( fd < 0 )
    {
        printf( "  socket() failed, errno=%d\n", errno );
        return;
    }
    FD_ZERO( &rd );
    FD_SET( fd, &rd );
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    rc = select( fd + 1, &rd, 0, 0, &tv );
    printf( "  select on an unconnected socket, 1 s -> %d (0 = waited and timed out), errno=%d\n",
            rc, errno );
    close( fd );
}

int main( void )
{
    printf( "== OS/2 API probe ==\n" );
    link_survey();
    semaphore_behaviour();
    select_needs_a_timeout();
    return g_sink == 0 ? 0 : 0;
}
