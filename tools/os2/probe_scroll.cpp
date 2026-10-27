/* probe_scroll.cpp -- does this box's VIO survive VioScrollUp, and in which shape?

   Context (TESTING.md, "The kernel still drew the screen; now it gets to
   scroll it instead"): candidate9 put a scroll shortcut in the flush -- a
   frame that is the previous one shifted up goes out as one VioScrollUp plus
   the new rows.  The first scrolled frame of the first streamed answer froze
   the box so hard the process could not be killed and only a reboot answered.

   The call carried every shape FTE's editor uses -- seven parameters, values
   where values, the rectangle arithmetic (~/code/fte/src/con_os2.cpp,
   ConScroll) -- except two habits worth isolating:

     1. candidate9 passed NULL for the attribute.  FTE always passes a
        pointer to a cell.  If eCS's VIO server reads the attribute without
        checking, that is a kernel-mode read of address 0, and sessions do
        not come back from those.
     2. candidate9 issued VioWrtCellStr's for the exposed rows immediately
        after the (asynchronous) scroll.  FTE does the same, but "the same"
        is exactly what this probe is allowed to prove and a candidate build
        is not.

   So: six shapes, run one at a time, every step logged to pscroll.log
   *before* the call that might end the world.  A hard lock survives the
   reboot in that log; read it back and the last line names the weapon.

   Run at the box's own screen, in a session with one:

        pscroll a        full-screen rectangle, 3 lines, pAttr NULL
                         -- candidate9's call exactly.
        pscroll b        same, pAttr -> a cell (FTE's).  The fix if a locks.
        pscroll c        inner rectangle (rows 1 .. rows-2), pAttr -> cell.
        pscroll d        full screen, 1 line, pAttr NULL.
        pscroll e        a, plus the exposed rows written immediately after --
                         the driver's flush sequence in full.
        pscroll m        e, plus the app's mouse habits around it (MouOpen,
                         MouGetNumQueEl polled before and after each scroll).

   The screen fills with numbered rows; five iterations scroll and rewrite,
   a pause between so a lock's position in the pattern is visible too.  A
   clean exit prints DONE to the log; the shell's text comes back on the
   next Return.  If it locks: reboot, and `type pscroll.log` answers.

   Build:  build.cmd link probe_scroll.cpp pscroll.exe
*/

#define INCL_DOSPROCESS
#define INCL_DOSERRORS
#define INCL_VIO
#define INCL_KBD
#define INCL_MOU

#include <os2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The tvision driver's abbreviations; this file has no tv.h to borrow them from. */
typedef unsigned char  uchar;
typedef unsigned short ushort;

static FILE *lg = 0;

static void step( const char *what, unsigned long rc )
{
    fprintf( stderr, "%s rc=%lu\n", what, rc );
    if( lg != 0 )
    {
        fprintf( lg, "%s rc=%lu\n", what, rc );
        fflush( lg );
    }
}

/* One screen row of cells: `d` down every column, a readable column count
   after it, so a scroll that moves rows is visible and one that eats them
   is more so. */
static void drawRow( uchar *row, ushort cols, int d, uchar attr )
{
    for( ushort x = 0; x < cols; ++x )
    {
        row[x * 2]     = (x < 6) ? (uchar) ( '0' + (d % 10) ) : ' ';
        row[x * 2 + 1] = attr;
    }
}

int main( int argc, char **argv )
{
    const char v = (argc > 1) ? argv[1][0] : 'a';
    VIOMODEINFO mode;
    APIRET rc;
    char hdr[64];

    lg = fopen( "pscroll.log", "a" );
    _snprintf( hdr, sizeof( hdr ), "=== variant '%c' start ===", v );
    step( hdr, 0 );

    memset( &mode, 0, sizeof( mode ) );
    mode.cb = sizeof( mode );
    rc = VioGetMode( &mode, 0 );
    step( "getmode", rc );
    if( rc != NO_ERROR || mode.row < 10 || mode.col < 20 )
    {
        step( "abort: no usable screen", 0 );
        return 1;
    }
    const ushort rows = mode.row, cols = mode.col;

    uchar *row = new uchar[(size_t) cols * 2];
    ushort fillCell = (ushort) ( 0x07 << 8 );   // FTE's shape: attribute high
    if( row == 0 )
    {
        step( "abort: no memory", 0 );
        return 1;
    }

    for( ushort y = 0; y < rows; ++y )
    {
        drawRow( row, cols, y, 0x07 );
        VioWrtCellStr( (PCH) row, (USHORT) ( cols * 2 ), y, 0, 0 );
    }
    step( "drawn", 0 );

    HMOU mou = 0;
    int haveMouse = 0;
    if( v == 'm' )
    {
        rc = MouOpen( 0, &mou );
        step( "mouopen", rc );
        haveMouse = ( rc == NO_ERROR );
    }

    for( int iter = 0; iter < 5; ++iter )
    {
        if( haveMouse )
        {
            MOUQUEINFO q;
            MouGetNumQueEl( &q, mou );
        }

        rc = 0xFFFF;
        switch( v )
        {
        case 'a':
            rc = VioScrollUp( 0, 0, (USHORT) ( rows - 1 ), (USHORT) ( cols - 1 ),
                              3, (PBYTE) 0, 0 );
            break;
        case 'b':
            rc = VioScrollUp( 0, 0, (USHORT) ( rows - 1 ), (USHORT) ( cols - 1 ),
                              3, (PBYTE) &fillCell, 0 );
            break;
        case 'c':
            rc = VioScrollUp( 1, 0, (USHORT) ( rows - 2 ), (USHORT) ( cols - 1 ),
                              3, (PBYTE) &fillCell, 0 );
            break;
        case 'd':
            rc = VioScrollUp( 0, 0, (USHORT) ( rows - 1 ), (USHORT) ( cols - 1 ),
                              1, (PBYTE) 0, 0 );
            break;
        default:  // 'e' and 'm': the driver's full sequence
            rc = VioScrollUp( 0, 0, (USHORT) ( rows - 1 ), (USHORT) ( cols - 1 ),
                              3, (PBYTE) 0, 0 );
            if( rc == NO_ERROR )
                for( int r = 0; r < 3; ++r )
                {
                    drawRow( row, cols, 90 + iter * 10 + r, 0x1E );
                    VioWrtCellStr( (PCH) row, (USHORT) ( cols * 2 ),
                                   (USHORT) ( rows - 3 + r ), 0, 0 );
                }
            break;
        }

        if( haveMouse )
        {
            MOUQUEINFO q;
            MouGetNumQueEl( &q, mou );
        }

        char msg[32];
        _snprintf( msg, sizeof( msg ), "iter %d scrolled", iter );
        step( msg, rc );
        DosSleep( 150 );
    }

    if( haveMouse )
        MouClose( mou );
    step( "DONE", 0 );
    if( lg != 0 )
        fclose( lg );
    return 0;
}
