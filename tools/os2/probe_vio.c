/* Capability probe: does VIO work for a 32-bit process, in the kind of session
   this port has to run in?

   VIO is the chosen display for OS/2, so the questions are whether it is
   reachable at all and whether it works in a session that a telnet server
   spawned -- which is how every program in this session has been run so far.
   A VIO entry point that links but answers only in a real foreground session
   would be worth knowing before writing a backend against it.

   Getting here cost two of my own mistakes, recorded because both are the kind
   that end a port: Watcom imports these under the upper-case names the headers
   define them to (VioGetMode is VIO16GETMODE), so a case-sensitive search for
   "Vio" in os2386.lib found nothing and I concluded for an afternoon that VIO
   was uncallable from 32-bit code.  It is: 249 VIO16* entry points are in the
   library.  And APIENTRY16 is _Far16 _Pascal, which in flat model is simply
   Pascal -- the "16" is history, not a call gate.

   What this measures, in the order a backend would care:

     VioGetMode        screen rows, columns, colour -- geometry
     VioGetBuf         the logical screen buffer, which is the fast write path
     VioGetCurPos/Type where the cursor is and what it looks like
     VioGetAnsi        whether this session interprets ANSI escapes -- the
                       difference between the ANSI option needing a driver or
                       not
     write and read    a marked string on the last row, read back and compared,
                       then restored: proof of a real writable screen rather
                       than an entry point that returns success
     KbdPeek           keyboard without waiting -- VioReadCharStr is deliberately
                       not called, because its parameter list has no wait flag
                       and a probe that blocks is a probe nobody can read
     MouOpen/GetNumButtons/GetPtrPos   mouse presence, without touching the
                       event queue, which would block

   printf goes to the log (stdout is redirected when this runs) while VIO writes
   go to the session's screen, so the report can be read over telnet even though
   the marker appears on the box's own monitor.

   Build (Open Watcom, OS/2):

     wcc386 -bt=os2 -5 -od -w3 -fo=probe_vio.obj probe_vio.c
     wlink system os2v2 name probe_vio.exe file probe_vio.obj library os2386.lib
*/

#define INCL_VIO
#define INCL_KBD
#define INCL_MOU
#define INCL_DOSERRORS
#include <os2.h>

#include <stdio.h>
#include <string.h>

/* VIO/KBD/MOU selector 1: this session's default screen and keyboard. */
#define SEL_DEFAULT   1

#define MARKER        "ATUI-VIO-PROBE"
#define MARKER_ATTR   0x1E        /* yellow on blue: obvious, and restored after */

/* Cells are two bytes: the character then its attribute. */
#define CELL_BYTES( n )   ( (USHORT) ( (n) * 2 ) )

static void report( const char *what, ULONG rc )
{
    printf( "  %-24s -> rc=%u%s\n", what, rc, rc == NO_ERROR ? "" : "   <-- failed" );
}

int main( void )
{
    VIOMODEINFO     mode;
    VIOCURSORINFO   cursor;
    KBDKEYINFO      keyInfo;
    UCHAR           cells[ 160 ];               /* saved screen row, cells */
    UCHAR           marker[ sizeof( MARKER ) * 2 ];
    UCHAR           readback[ 160 ];
    UCHAR           one[ 2 ];
    PCH             viewBuffer = 0;
    USHORT          viewLength = 0;
    USHORT          row = 0, col = 0;
    USHORT          ansi = 0xFFFF;
    USHORT          buttons = 0;
    HKBD            hKeyboard = 0;
    HMOU            hMouse = 0;
    ULONG           rc = 0;
    size_t          i = 0;

    printf( "== VIO from a 32-bit process, in this session ==\n" );

    memset( &mode, 0, sizeof( mode ) );
    mode.cb = sizeof( VIOMODEINFO );
    rc = VioGetMode( &mode, SEL_DEFAULT );
    report( "VioGetMode (selector 1)", rc );
    if( rc != NO_ERROR )
    {
        /* 436 is ERROR_VIO_INVALID_HANDLE: either this process has no VIO
           session at all -- a background session, which is what a telnet server
           gives -- or the selector is wrong.  The system-window selector
           distinguishes them, and 436 again means there is no screen here rather
           than the wrong number being passed. */
        VIOMODEINFO alt;
        memset( &alt, 0, sizeof( alt ) );
        alt.cb = sizeof( VIOMODEINFO );
        report( "VioGetMode (0xFFFE)", VioGetMode( &alt, 0xFFFE ) );
    }
    if( rc == NO_ERROR )
        printf( "      rows=%u cols=%u color=%u fbType=%u\n",
                mode.row, mode.col, mode.color, mode.fbType );

    memset( &cursor, 0, sizeof( cursor ) );
    report( "VioGetCurType", VioGetCurType( &cursor, SEL_DEFAULT ) );
    printf( "      cursor yStart=%u cEnd=%u cx=%u\n", cursor.yStart, cursor.cEnd, cursor.cx );

    report( "VioGetCurPos", VioGetCurPos( &row, &col, SEL_DEFAULT ) );
    printf( "      cursor at row=%u col=%u\n", row, col );

    report( "VioGetBuf", VioGetBuf( (PULONG) &viewBuffer, &viewLength, SEL_DEFAULT ) );
    printf( "      logical buffer=%p length=%u\n", (void *) viewBuffer, viewLength );

    report( "VioGetAnsi", VioGetAnsi( &ansi, SEL_DEFAULT ) );
    printf( "      ANSI interpretation in this session: %s\n",
            ansi == 0xFFFF ? "off" : ansi == 0 ? "on" : "?" );

    /* Write, read back, restore.  Only worth doing on a screen that answered
       VioGetMode, and only with room to do it. */
    if( rc == NO_ERROR && mode.row > 0 && mode.col >= (USHORT)( sizeof( MARKER ) ) )
    {
        USHORT  textLen = (USHORT) strlen( MARKER );
        USHORT  readLen = CELL_BYTES( mode.col );
        USHORT  savedRow = row, savedCol = col;

        memset( cells, 0, sizeof( cells ) );
        report( "VioReadCellStr (save)",
                VioReadCellStr( (PCH) cells, &readLen, mode.row - 1, 0, SEL_DEFAULT ) );

        for( i = 0; i < textLen; ++i )
        {
            marker[ i * 2 ]     = (UCHAR) MARKER[ i ];
            marker[ i * 2 + 1 ] = MARKER_ATTR;
        }
        report( "VioWrtCellStr (marker)",
                VioWrtCellStr( (PCH) marker, textLen, mode.row - 1, 0, SEL_DEFAULT ) );

        memset( readback, 0, sizeof( readback ) );
        readLen = CELL_BYTES( textLen );
        report( "VioReadCellStr (read back)",
                VioReadCellStr( (PCH) readback, &readLen, mode.row - 1, 0, SEL_DEFAULT ) );
        printf( "      read back [%.*s] -- %s\n", (int) textLen, readback,
                memcmp( readback, marker, CELL_BYTES( textLen ) ) == 0
                    ? "matches what was written"
                    : "DIFFERS from what was written" );

        report( "VioWrtCellStr (restore)",
                VioWrtCellStr( (PCH) cells, textLen, mode.row - 1, 0, SEL_DEFAULT ) );
        VioSetCurPos( savedRow, savedCol, SEL_DEFAULT );

        /* One cell through the fill-style call, which is how a cleared line
           would be drawn. */
        one[ 0 ] = ' ';
        one[ 1 ] = 0x07;
        report( "VioWrtNCell (fill)",
                VioWrtNCell( (PBYTE) one, 1, mode.row - 1, 0, SEL_DEFAULT ) );
    }

    printf( "== keyboard and mouse ==\n" );

    /* VioReadCharStr is not measured: its parameter list is (buffer, count,
       row, column, selector) with no wait flag, so it blocks for a key and a
       probe that blocks is a probe nobody can read.  A UI either accepts a
       blocking read on a background thread or uses the Kbd family, where
       KbdCharIn takes KBD_WAIT and KbdPeek never waits.  Only the non-blocking
       one is called here, and no call that would discard the session's typed
       input (KbdFlushBuffer) is made at all. */
    /* Open first, then peek with the handle that opening gave.  The first
       version passed the selector constant here and got ERROR_KBD_INVALID_HANDLE
       (439), which reads like a platform answer and is a probe answer. */
    hKeyboard = 0;
    report( "KbdOpen", KbdOpen( &hKeyboard ) );
    if( hKeyboard != 0 )
        printf( "      keyboard handle %u\n", (unsigned) hKeyboard );

    memset( &keyInfo, 0, sizeof( keyInfo ) );
    rc = KbdPeek( &keyInfo, hKeyboard );
    report( "KbdPeek (non-blocking)", rc );
    if( rc == NO_ERROR )
        printf( "      a key is pending: char=0x%02x scan=0x%02x\n",
                keyInfo.chChar, keyInfo.chScan );
    else
        printf( "      nothing pending (or no keyboard) -- rc says which\n" );

    rc = MouOpen( 0, &hMouse );
    report( "MouOpen", rc );
    if( rc == NO_ERROR )
    {
        PTRLOC where;
        report( "MouGetNumButtons", MouGetNumButtons( &buttons, hMouse ) );
        printf( "      buttons=%u\n", buttons );
        memset( &where, 0, sizeof( where ) );
        report( "MouGetPtrPos", MouGetPtrPos( &where, hMouse ) );
        printf( "      pointer at col=%u row=%u\n", where.col, where.row );
        MouClose( hMouse );
    }

    printf( "== what this answers ==\n" );
    printf( "  If VioGetMode and the write/read pair above succeeded, a VIO\n" );
    printf( "  backend can be written against these entry points and run in a\n" );
    printf( "  session like this one.  If they failed, VIO needs a foreground\n" );
    printf( "  session and testing has to happen on the box's own screen.\n" );
    return 0;
}
