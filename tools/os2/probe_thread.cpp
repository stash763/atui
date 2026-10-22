/* Does this target's C run time have threads? Answer before depending on it.
 *
 * PORTING.md says OS/2 threading goes through `_beginthread` rather than
 * `DosCreateThread`, because the run time has per-thread state -- a thread-data block,
 * the signal/handler chain, the FPU state -- that only the C runtime's own starter
 * installs.  Open Watcom's source agrees (`clib/mthread/c/thredo32.c`: DosCreateThread,
 * then `__alloca( __ThreadDataSize )`, `__OS2AddThread`, `_fpreset`, `__sig_init_rtn`),
 * but that file carries a 2017 copyright and the compiler on the box is 1.9.  Whether
 * 1.9 shipped it for `-bt=os2v2`, and whether `-bm` is what selects it, is a question
 * about the libraries on this machine, so this asks the machine.
 *
 * Four things, in the order they would hurt:
 *
 *   1. `_beginthread` compiles and links -- header and library, not just header.
 *   2. Two threads run at the same time (if they were cooperative fakes, or the call
 *      silently failed, the counts say so).
 *   3. The heap survives two threads allocating -- what atui's worker actually does, a
 *      turn being made of std::string and std::vector.  The run time protects the heap
 *      through function pointers (`_AccessFHeap` et al.) that `__InitMultipleThread()`
 *      installs when the first `_beginthread` happens, so the question is whether that
 *      machinery is in the library this compiler shipped and does what the source says.
 *   4. What the compiler was told: `__MT__`, which the run time uses to compile its own
 *      locking in, versus ATUI_HAVE_OS2_THREADS, which the build uses to decide to call
 *      `_beginthread` at all.  Measured here: `-bm` does not define `__MT__`.
 *
 * Two things it deliberately does not do, both learned the hard way.  It does not
 * accumulate: an earlier version kept every chunk in a vector, so 20,000 chunks per
 * thread was really a test of how much memory the box has, and it died in ways that
 * looked exactly like heap corruption.  Blocks are allocated, used, and released, so
 * the footprint is flat and the only thing under test is the allocator under
 * contention.  And it does not go quietly: a line every so often says how far it got,
 * because a probe that dies without a last word leaves the reader with two theories and
 * no way to choose.
 *
 * Build and run:
 *
 *   build.cmd thrprobe probe_thread.cpp pthr.exe           plain runtime
 *   build.cmd thrmt   probe_thread.cpp pthr.exe            with -bm
 *   pthr.exe                          paced, quick
 *   pthr.exe 20000 hammer             contend hard
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <process.h>            // _beginthread: the question this probe asks

#include <string>
#include <vector>

extern "C" unsigned long _System DosSleep( unsigned long msec );

#ifndef ATUI_STACK
#define ATUI_STACK 65536
#endif

namespace {

volatile long g_counter = 0;
volatile long g_running = 0;
volatile long g_reached = 0;        // how far the last thread got: the last word
long g_chunksPerThread = 400;
bool g_hammer = false;              // no sleeps: make the allocator contend

/* One worker: allocate and release, keep a few strings alive across a sleep so a block
 * is demonstrably this thread's, and say how far it got every so often. */
void worker( void *arg )
{
    const int id = (int) (long) arg;
    std::string held, twice;
    std::vector< std::string > keep;
    keep.resize( 8 );

    for( long i = 0; i < g_chunksPerThread; ++i )
    {
        char buf[64];
        std::sprintf( buf, "thread %d chunk %ld padding padding padding", id, i );
        held = buf;                       // allocate
        twice = held + held;              // allocate again, bigger
        keep[ (size_t) ( i & 7 ) ] = twice;   // replace: free and allocate
        ++g_counter;
        g_reached = i;
        if( !g_hammer )
        {
            if( ( i & 7 ) == 0 )
                DosSleep( 1 );
        }
        else if( ( i & 2047 ) == 0 )
        {
            printf( "  thread %d at %ld\n", id, i );
            fflush( stdout );
        }
    }
    printf( "  thread %d done at %ld (kept %d bytes)\n", id, g_reached,
            (int) keep.size() + (int) twice.size() );
    fflush( stdout );
    --g_running;
}

} // anonymous namespace

int main( int argc, char **argv )
{
    printf( "probe_thread: __MT__=%s ATUI_HAVE_OS2_THREADS=%s __WATCOMC__=%ld\n",
#if defined( __MT__ )
            "defined",
#else
            "not defined",
#endif
#if defined( ATUI_HAVE_OS2_THREADS )
            "defined",
#else
            "not defined",
#endif
            (long) __WATCOMC__ );

    if( argc > 1 )
        g_chunksPerThread = atol( argv[ 1 ] );
    if( argc > 2 && strcmp( argv[ 2 ], "hammer" ) == 0 )
        g_hammer = true;
    printf( "  chunks/thread=%ld %s\n", g_chunksPerThread, g_hammer ? "hammer" : "paced" );
    fflush( stdout );

    int started = 0;
    g_running = 2;
    for( int i = 0; i < 2; ++i )
    {
        const int rc = _beginthread( worker, NULL, ATUI_STACK, (void *) (long) i );
        printf( "  _beginthread -> %d%s\n", rc, rc == -1 ? " (FAILED)" : "" );
        if( rc != -1 )
            ++started;
    }
    fflush( stdout );

    if( started == 0 )
    {
        printf( "RESULT: no threads available\n" );
        return 1;
    }

    // Generous, and generous for a reason: with `hammer` on a slow box the work is real
    // work, and a watchdog that fires first would report "threads do not work" for a
    // machine that is merely slow -- the worst kind of wrong answer from a probe.
    const time_t until = time( NULL ) + 600;
    while( g_running > 0 && time( NULL ) < until )
        DosSleep( 20 );

    const bool timedOut = g_running > 0;
    printf( "threads started=%d counter=%ld (want %ld) reached=%ld running=%d%s\n",
            started, (long) g_counter, 2L * g_chunksPerThread, g_reached,
            (int) g_running, timedOut ? " -- WATCHDOG" : "" );
    fflush( stdout );

    const bool ok = started == 2 && g_counter >= 2L * g_chunksPerThread && !timedOut;
    printf( "RESULT: %s\n", ok ? "threads work" : "threads do not work" );
    fflush( stdout );
    return ok ? 0 : 1;
}
