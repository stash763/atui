/* Stream-path proof and key map, in one program.

   What this is for.  The OS/2 backend has to draw with VT escapes and read keys
   off the console, and two things about that can be known only by running
   something on the machine:

     * whether a screen assembled from escapes actually renders as intended once
       the bytes have travelled through the telnet server to a terminal emulator.
       Bytes passing through untouched was measured already; a correct *screen* is
       a stronger claim, and it is the one the port rests on.
     * what each key really arrives as.  Measured already: `ESC [ A` sent from the
       other end reached the program as 224 then 72 -- a BIOS extended code -- so
       the session translates escapes into DOS key codes on the way in.  Every key
       the interface uses has to be in that table, and inventing the table instead
       of measuring it is how a port ends up with an arrow key that works only on
       Tuesdays.

   So this draws a recognisable frame -- border, title, status line -- and reports
   every keystroke as the raw codes that arrived, in hex, both on screen and in
   the log.  The screen is the render test; the log is the key map.

   Deliberately plain C with no tvision in it: if the frame is wrong, the escapes
   or the session are wrong, and nothing else could say so.  ASCII on screen
   throughout, because the character set is a separate variable and this test must
   not depend on it.

   Two things the input loop knows because they were measured rather than assumed:
   `select()` fails on the console handle, so readiness comes from polling
   `kbhit()`; and the two halves of an extended code can arrive more than 200 ms
   apart, so the prefix is held and waited for instead of a key being declared
   unknown at the first gap.

   Build (Open Watcom, OS/2):

     wcc386 -bt=os2 -5 -od -w3 -fo=sk.obj stream_keys.c
     wlink system os2v2 name sk.exe file sk.obj

   Run it over the relay and feed it keys; it exits on `q` or after 90 seconds.
*/

#define INCL_DOSPROCESS
#include <os2.h>

#include <stdio.h>
#include <string.h>
#include <io.h>
#include <conio.h>

#define COLS        80
#define ROWS        25
#define POLL_MS     100
#define LIMIT_MS    90000
#define GAP_CAP     1200       /* how long to wait for the next byte of one key */

static void place( int row, int col )
{
    printf( "\x1b[%d;%dH", row + 1, col + 1 );
}

/* The frame uses the VT alternate character set rather than high-bit bytes:
   escapes survive the session untouched, while high-bit bytes depend on the
   terminal type the server was told to expect.  A frame built from escapes is
   therefore testing the escapes instead of the negotiation. */
static void draw_frame( void )
{
    int col = 0;
    int row = 0;

    fputs( "\x1b[2J\x1b[H", stdout );

    place( 0, 0 );
    fputs( "\x1b(0l", stdout );
    for( col = 1; col < COLS - 1; ++col )
        fputc( 'q', stdout );
    fputs( "k\x1b(B", stdout );

    for( row = 1; row < ROWS - 1; ++row )
    {
        place( row, 0 );
        fputs( "\x1b(0x\x1b(B", stdout );
        place( row, COLS - 1 );
        fputs( "\x1b(0x\x1b(B", stdout );
    }

    place( ROWS - 1, 0 );
    fputs( "\x1b(0m", stdout );
    for( col = 1; col < COLS - 1; ++col )
        fputc( 'q', stdout );
    fputs( "j\x1b(B", stdout );

    place( 0, 4 );
    fputs( " atui stream path: what the keys look like ", stdout );
    place( 2, 4 );
    fputs( "press keys; each one is shown as the codes that arrived", stdout );
    place( 3, 4 );
    fputs( "q quits", stdout );
    fflush( stdout );
}

static FILE *gLog = NULL;

static void report( int seq, const int *codes, int count, const int *gaps )
{
    int i = 0;

    place( 5, 4 );
    printf( "seq %-3d codes", seq );
    for( i = 0; i < count; ++i )
        printf( " %02X", codes[ i ] );
    for( i = count; i < 6; ++i )
        fputs( "   ", stdout );
    fflush( stdout );

    /* The record goes to a file, not to stdout: stdout *is* the screen here, and
       printing the log onto it scrolls the very frame being tested.  The file is
       fetched afterwards, which keeps the render test and the key map from
       interfering with each other.

       Each gap is the milliseconds it took for that byte to become available
       after the one before it, capped at GAP_CAP -- 1200 means it never turned
       up, which is how a split key shows itself in the data. */
    if( gLog != NULL )
    {
        fprintf( gLog, "KEY seq=%d count=%d", seq, count );
        for( i = 0; i < count; ++i )
            fprintf( gLog, " %02X", codes[ i ] );
        fprintf( gLog, " | gaps" );
        for( i = 1; i <= count; ++i )
        {
            if( gaps[ i ] < 0 )
                fprintf( gLog, " -" );
            else
                fprintf( gLog, " %d", gaps[ i ] );
        }
        fprintf( gLog, "\n" );
        fflush( gLog );
    }
}

/* How long until the next byte of this key is available, in milliseconds, up to
   a generous cap.  Measured rather than assumed: the first reader used a fixed
   window and split every translated key in half, because it decided the key was
   over before the second byte had arrived, and the report after that was wrong
   too -- a boundary error that misaligns everything downstream. */
static int gapToNextKey( int capMs )
{
    int waited = 0;
    while( !kbhit() && waited < capMs )
    {
        DosSleep( 10 );
        waited += 10;
    }
    return kbhit() ? waited : capMs;
}

/* One key, parsed rather than slurped.

   The shape is decided by what the first byte means: 0 or 224 is a DOS extended
   code and takes one more byte; ESC starts a sequence, so take the introducer and
   parameters up to a final byte.  Each continuation is waited for up to GAP_CAP
   and the wait actually taken is reported, so this doubles as the measurement of
   how far apart the halves of one key can land -- the number the real reader's
   buffering has to be built around. */
static int read_one_key( int *codes, int max, int *gaps )
{
    int count = 0;
    int c = getch();

    codes[ count++ ] = c;
    gaps[ 0 ] = 0;

    if( c == 0 || c == 224 )
    {
        gaps[ count ] = gapToNextKey( GAP_CAP );
        if( count < max && kbhit() )
            codes[ count++ ] = getch();
    }
    else if( c == 0x1B )
    {
        gaps[ count ] = gapToNextKey( GAP_CAP );
        if( !kbhit() )
            return count;                       /* a bare Escape */
        codes[ count++ ] = getch();

        if( count < max && ( codes[ 1 ] == '[' || codes[ 1 ] == 'O' ) )
        {
            for( ;; )
            {
                int last = 0;
                gaps[ count ] = gapToNextKey( GAP_CAP );
                if( !kbhit() || count >= max )
                    break;
                codes[ count++ ] = getch();
                last = codes[ count - 1 ];
                if( !( ( last >= '0' && last <= '9' ) || last == ';' ) )
                    break;
            }
        }
    }

    return count;
}

int main( void )
{
    int seq = 0;
    int elapsed = 0;

    /* The key record goes to a file next to the program, fetched afterwards with
       `tools/atui-remote get d:\atui\keys.log`. */
    gLog = fopen( "keys.log", "w" );

    draw_frame();

    while( elapsed < LIMIT_MS )
    {
        int codes[ 8 ];
        int gaps[ 10 ];
        int count = 0;
        int g = 0;

        /* -1 means "not measured", and report() prints it as such.  Left
           uninitialised the first time, the array printed 332693 in place of a
           gap and made the numbers next to the real ones worthless. */
        for( g = 0; g < 10; ++g )
            gaps[ g ] = -1;

        if( !kbhit() )
        {
            DosSleep( POLL_MS );
            elapsed += POLL_MS;
            continue;
        }

        count = read_one_key( codes, 8, gaps );
        if( codes[ 0 ] == 'q' )
            break;

        ++seq;
        report( seq, codes, count, gaps );
    }

    fputs( "\x1b[H\x1b[0m\x1b[J", stdout );
    printf( "STREAM-KEYS-DONE reports=%d\n", seq );
    fflush( stdout );
    if( gLog != NULL )
        fclose( gLog );
    return 0;
}
