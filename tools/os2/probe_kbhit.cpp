/* Does kbhit() block?  The whole UI wake-up story depends on the answer.
 *
 * atui's OS/2 input layer (tvision's hardwvos2.cpp, our fork) asks whether a keystroke
 * is waiting with kbhit(), in a loop bounded by the timeout tvision was given:
 *
 *     while( !anyInput() ) { if( waited >= timeoutMs ) break; DosSleep( kPollMs ); }
 *
 * That loop only honours the timeout if the readiness test returns.  Watcom's kbhit() is
 * KbdPeek() -- the 16-bit physical-keyboard call, behind a default-windowing hook in a
 * windowed session -- and the app is driven over telnet, where stdin is whatever the
 * session hands it rather than a keyboard.  Measured in the app: the UI thread logged no
 * timer event for 317 seconds while a turn ran on a worker, and drew everything in a
 * burst when the session closed.  Consistent with the wait never returning; consistent
 * too with several other things, which is why this asks the primitive instead of the app.
 *
 * The measurement is wall-clock against a stamped line per call, which is the cheapest
 * thing that discriminates: 60 calls at a 10 ms cadence take well under a second, so a
 * line stamped +40 means the call before it slept for 40 seconds.  Millisecond timing
 * would need a clock this probe has no business pulling in, and would not change the
 * answer -- the question is "returns" versus "does not".
 *
 *   build.cmd thrprobe probe_kbhit.cpp pkb.exe
 *   pkb.exe          nobody types; that is the point
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <conio.h>
#include <io.h>

extern "C" unsigned long _System DosSleep( unsigned long msec );

int main( int argc, char **argv )
{
    const int kCalls = ( argc > 1 ) ? atoi( argv[ 1 ] ) : 60;
    const time_t start = time( NULL );

    printf( "probe_kbhit: isatty(0)=%d, %d calls at a 10ms cadence (expect <1s total)\n",
            isatty( 0 ), kCalls );
    fflush( stdout );

    int blockedAt = -1;
    for( int i = 0; i < kCalls; ++i )
    {
        const int ready = kbhit();
        const long elapsed = (long) ( time( NULL ) - start );
        printf( "  #%2d kbhit -> %d at +%ld s\n", i, ready, elapsed );
        fflush( stdout );
        if( elapsed > 2 && blockedAt < 0 )
            blockedAt = i;
        DosSleep( 10 );     // the same cadence hardwvos2.cpp's wait loop uses
    }

    // And the loop tvision actually runs, to the same cadence it asks for.
    const time_t waitStart = time( NULL );
    int loops = 0;
    while( !kbhit() )
    {
        if( loops >= 5 )          // 5 x 10ms ~= the 50ms tvision asks for
            break;
        DosSleep( 10 );
        ++loops;
    }
    const long waitElapsed = (long) ( time( NULL ) - waitStart );
    printf( "one tvision-shaped 50ms wait: %d loops, %ld second(s)\n", loops, waitElapsed );
    fflush( stdout );

    const bool ok = blockedAt < 0 && waitElapsed <= 1;
    printf( "RESULT: %s\n",
            ok ? "kbhit returns; the wait can be bounded, so look elsewhere"
               : "kbhit does not return when asked to" );
    fflush( stdout );
    return ok ? 0 : 1;
}
