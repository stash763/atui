/* Capability probe: is there a VIO screen here, what size is it, and does what
   this program writes on it reach the telnet client?

   VIO is the display this port has to have: it is the only way to draw at the
   machine's own screen, and the only way to be told the real geometry -- the
   stream back end has to be *told* its size through ATUI_COLS/ATUI_ROWS because
   a byte stream cannot carry a question about a window nobody owns.

   Getting here cost a mistake of mine, recorded because it nearly ended the
   port.  The first version of this probe passed 1 as the session handle to every
   VIO call, got 436 (ERROR_VIO_INVALID_HANDLE) back, and I read that as "a
   session a telnet server spawns has no screen" and wrote the back end to
   match.  1 is not the default handle; 0 is, and 1 is not a handle at all --
   "selector" is a word from the 16-bit guarded-segment era that these entry
   points grew out of, not an instruction.  KbdOpen(0) and MouOpen(0) having
   succeeded in the very same run should have given it away: a process with no
   session does not get a keyboard and a mouse.

   A second thing this probe had wrong is the entry points themselves, and the
   toolkit headers settled it rather than guesswork: the write call is
   VioWrtCellStr -- the 1.x name, which OS/2 2.x kept -- not VioWriteCellStr, and
   the sizes are USHORT where a 32-bit API would use PULONG (VioGetBuf's length,
   VioReadCellStr's count in and out, VioGetAnsi's flag).  All of these are
   APIRET16 APIENTRY16: the VIO API never left the 16-bit interface, which in
   Watcom's flat model is only a calling-convention detail.

   What this measures, in the order a back end would care:

     VioGetMode        rows, columns, colour -- the geometry, from the system
     VioGetCurPos      where the cursor is, which is where a program has to
                       leave it when it puts the screen back
     VioGetBuf         the logical screen buffer, the fast write path
     write/read        a marked string written on the last row, read back and
                       compared, then restored: proof of a real screen
     VioGetAnsi        whether this session interprets escape sequences
     KbdPeek           keyboard without waiting, which is what a poll loop needs
                       (VioReadCharStr is not called: its parameter list has no
                       wait flag, and a probe that blocks is a probe nobody can
                       read)

   Between writing the marker and restoring it the probe holds it up for a few
   seconds.  That gap is the point: whatever the telnet client shows during it is
   the answer to whether the server translates this session's screen -- the
   difference between VIO being the one display for both the machine's own
   monitor and a remote session, and being only the monitor.  So run this with
   its stdout redirected to a file: then nothing but VIO can put that marker on
   the screen in front of you, or on the screen in front of a telnet client.

   Build: `build.cmd link probe_vio.c pvio.exe`, or by hand --

     wcc386 -bt=os2 -5 -od -w3 -fo=probe_vio.obj probe_vio.c
     wlink system os2v2 name probe_vio.exe file probe_vio.obj library os2386.lib
*/

#define INCL_VIO
#define INCL_KBD
#define INCL_DOSERRORS
#include <os2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Session handle 0: this session's own screen and keyboard. */
#define SESSION_DEFAULT     0

#define MARKER              "ATUI-VIO-PROBE-OK"
#define MARKER_ATTR         0x1E            /* yellow on blue: obvious */
#define HOLD_MS             6000            /* long enough to look at */
#define SCRATCH             256             /* bytes: one 80-column row of cells */

static void report( const char *what, APIRET16 rc )
{
    printf( "  %-24s -> rc=%u%s\n", what, (unsigned)rc,
            rc == NO_ERROR ? "" : "   <-- failed" );
}

/* The whole screen as text.
   Whether a VIO back end can be verified from a distance depends on this: if
   what a program reads back is the text the operator's own window shows, then
   reading it back over a connection is the same as standing in front of the
   machine.  If it is not, this prints the proof of what is actually there. */
static void dumpScreen( const char *when, const VIOMODEINFO *mode )
{
    UCHAR   cells[ 80 * 2 ];
    UCHAR   line[ 81 ];
    USHORT  cb = 0;
    USHORT  r = 0;
    int     c = 0;
    int     width = (int)mode->col;

    if( width > 80 )
        width = 80;
    printf( "---- screen %s (%u rows x %u cols) ----\n", when,
            (unsigned)mode->row, (unsigned)mode->col );
    for( r = 0; r < mode->row; ++r )
    {
        cb = (USHORT)( width * 2 );
        memset( cells, 0, sizeof( cells ) );
        if( VioReadCellStr( (PCH)cells, &cb, r, 0, SESSION_DEFAULT ) != NO_ERROR )
        {
            printf( "%2u| <VioReadCellStr failed> |\n", r );
            break;
        }
        for( c = 0; c < width; ++c )
            line[ c ] = cells[ c * 2 ] ? cells[ c * 2 ] : ' ';
        line[ width ] = 0;
        printf( "%2u|%s|\n", r, (char *)line );
    }
    printf( "---------------------------------\n" );
    fflush( stdout );
}

/* A strip of characters a theme would want: CP437 shading and box drawing, the
   ASCII that means the same thing, and a couple of accented letters.  What the
   telnet client makes of them is what the port has to live with -- the machine's
   own screen shows the real CP437 glyphs, a translation on a wire may not. */
static void paintStrip( void )
{
    static const UCHAR glyphs[] = {
        0xB0, 0xB1, 0xB2, 0xDB, 0xB3, 0xBF, 0xD9, 0xC0, 0xC4, 0xC3,
        0xC5, 0xD4, 0xC6, 0xDD, 0xDE, 0xFE, 0x8A, 0xA0, '|', '-'
    };
    UCHAR   cells[ sizeof( glyphs ) * 2 ];
    UCHAR   saved[ 162 ];
    USHORT  cb = 0;
    size_t  i = 0;

    printf( "---- painting %u glyphs on row 1 ----", (unsigned)( sizeof( glyphs ) ) );
    printf( "\n" );
    cb = sizeof( saved ) - 2;
    memset( saved, 0, sizeof( saved ) );
    report( "VioReadCellStr (save row 1)",
            VioReadCellStr( (PCH)saved, &cb, 1, 0, SESSION_DEFAULT ) );
    for( i = 0; i < sizeof( glyphs ); ++i )
    {
        cells[ i * 2 ]     = glyphs[ i ];
        cells[ i * 2 + 1 ] = 0x07;
    }
    report( "VioWrtCellStr (glyphs)",
            VioWrtCellStr( (PCH)cells, (USHORT) sizeof( cells ), 1, 0,
                           SESSION_DEFAULT ) );
    fflush( stdout );
    DosSleep( 6000 );
    cb = (USHORT) sizeof( cells );
    memset( cells, 0, sizeof( cells ) );
    if( VioReadCellStr( (PCH)cells, &cb, 1, 0, SESSION_DEFAULT ) == NO_ERROR )
    {
        printf( "      buffer holds:" );
        for( i = 0; i < sizeof( glyphs ); ++i )
            printf( " %02X", cells[ i * 2 ] );
        printf( "\n" );
    }
    report( "VioWrtCellStr (restore row 1)",
            VioWrtCellStr( (PCH)saved, (USHORT)( sizeof( saved ) - 2 ), 1, 0,
                           SESSION_DEFAULT ) );
    fflush( stdout );
}

/* What a key looks like when it arrives, read the way a VIO poll loop would
   read it.  Typed here as well as typed there: over a connection the server has
   to turn what the client sends into a keyboard event, and which codes survive
   that is what an application's key handling has to be written against. */
static void watchKeys( ULONG ms )
{
    KBDKEYINFO  key;
    ULONG       waited = 0;
    int         n = 0;

    printf( "---- watching the keyboard for %u ms: type keys now ----\n", ms );
    fflush( stdout );
    while( waited < ms )
    {
        memset( &key, 0, sizeof( key ) );
        if( KbdPeek( &key, SESSION_DEFAULT ) == NO_ERROR )
        {
            printf( "  key %2d: ascii=0x%02X scan=0x%02X state=0x%04X\n",
                    ++n, key.chChar, key.chScan, key.fsState );
            fflush( stdout );
            /* fsWaitEcho has no names in these headers; Watcom's own conio
               calls it with 0, which is a waiting read with no echo.  Bit 0 is
               wait/no-wait, bit 1 echo. */
            KbdCharIn( &key, 0, SESSION_DEFAULT );
            if( key.chChar == 0x1B || key.chScan == 0x01 )
                break;
            waited = 0;
        }
        DosSleep( 50 );
        waited += 50;
    }
    printf( "---- done: %d key(s) ----\n", n );
    fflush( stdout );
}

/* Which rows of this screen can be repainted at all.
   The marker on the last row reached a telnet client; a row higher up did not.
   If the server translates the session's screen as a stream -- it can emit what
   comes after the cursor and nothing before it -- then only some rows are
   paintable, and a full screen application has to know that before it is
   written rather than discover it as a blank band across the middle of the
   display.  Each row is written, held, and put back, with the row number in the
   text and the wire timestamps as the evidence. */
static void scanRows( const VIOMODEINFO *mode )
{
    static const USHORT rows[] = { 24, 23, 20, 15, 10, 5, 1, 0 };
    UCHAR   saved[ 162 ];
    UCHAR   cells[ 64 ];
    char    label[ 24 ];
    USHORT  cb = 0;
    USHORT  curRow = 0, curCol = 0;
    size_t  i = 0;
    size_t  n = 0;

    printf( "---- writing one row at a time, 1.5 s each ----\n" );
    fflush( stdout );
    for( n = 0; n < sizeof( rows ) / sizeof( rows[ 0 ] ); ++n )
    {
        USHORT  r = rows[ n ];
        int     len = 0;

        if( r >= mode->row )
            continue;
        cb = sizeof( saved ) - 2;
        memset( saved, 0, sizeof( saved ) );
        if( VioReadCellStr( (PCH)saved, &cb, r, 0, SESSION_DEFAULT )
            != NO_ERROR )
            continue;
        len = sprintf( label, "ROW%02u-PAINT", (unsigned)r );
        for( i = 0; i < (size_t)len; ++i )
        {
            cells[ i * 2 ]     = (UCHAR)label[ i ];
            cells[ i * 2 + 1 ] = 0x1F;
        }
        VioGetCurPos( &curRow, &curCol, SESSION_DEFAULT );
        printf( "M%02u write (cursor was %u,%u)\n", (unsigned)r, curRow, curCol );
        fflush( stdout );
        VioWrtCellStr( (PCH)cells, (USHORT)( len * 2 ), r, 0, SESSION_DEFAULT );
        DosSleep( 1500 );
        VioWrtCellStr( (PCH)saved, cb, r, 0, SESSION_DEFAULT );
    }
    printf( "---- done ----\n" );
    fflush( stdout );
}

int main( int argc, char **argv )
{
    VIOMODEINFO     mode;
    VIOCURSORINFO   cursor;
    KBDKEYINFO      key;
    UCHAR           rowSave[ SCRATCH ];
    UCHAR           marker[ sizeof( MARKER ) * 2 ];
    UCHAR           readBack[ SCRATCH ];
    PCH             lvb = 0;                /* logical video buffer */
    USHORT          cbLVB = 0;
    USHORT          cb = 0;
    USHORT          row = 0, col = 0;
    USHORT          lastRow = 0;
    char            printable[ SCRATCH / 8 ];
    USHORT          ansi = 0xFFFF;
    APIRET16        rc = 0;
    size_t          i = 0;
    int             width = 0;

    printf( "== VIO in this session (handle 0) ==\n" );
    fflush( stdout );

    memset( &mode, 0, sizeof( mode ) );
    mode.cb = sizeof( VIOMODEINFO );
    rc = VioGetMode( &mode, SESSION_DEFAULT );
    report( "VioGetMode", rc );
    if( rc != NO_ERROR )
    {
        printf( "  no VIO screen: a stream display is all this session has\n" );
        fflush( stdout );
        return 1;
    }
    /* row and col are the COUNT of rows and columns, not the index of the last
       one: this session answered col=80 row=25 for the screen it has, and asking
       VioReadCellStr for row 25 came back 358 (ERROR_VIO_ROW).  A back end
       indexes 0..row-1 and stores the counts as the size. */
    printf( "      rows=%u cols=%u color=%u fbType=%u\n",
            mode.row, mode.col, (unsigned)mode.color, (unsigned)mode.fbType );

    /* Modes, so that one program answers the three questions that decide a
       back end's shape: how big is the screen, what does a glyph become on the
       way to a telnet client, and what does a key look like when it arrives. */
    if( argc > 1 && strcmp( argv[1], "--paint" ) == 0 )
    {
        paintStrip();
        return 0;
    }
    if( argc > 1 && strcmp( argv[1], "--keys" ) == 0 )
    {
        watchKeys( 30000 );
        return 0;
    }
    if( argc > 1 && strcmp( argv[1], "--rowscan" ) == 0 )
    {
        scanRows( &mode );
        return 0;
    }

    memset( &row, 0, sizeof( row ) );
    report( "VioGetCurPos", VioGetCurPos( &row, &col, SESSION_DEFAULT ) );
    printf( "      cursor at row=%u col=%u\n", row, col );

    memset( &cursor, 0, sizeof( cursor ) );
    report( "VioGetCurType", VioGetCurType( &cursor, SESSION_DEFAULT ) );
    printf( "      cursor first scanline=%u last scanline=%u width=%d\n",
            cursor.yStart, cursor.cEnd, cursor.cx );

    report( "VioGetBuf", VioGetBuf( (PULONG)&lvb, &cbLVB, SESSION_DEFAULT ) );
    printf( "      logical buffer=%p bytes=%u\n", (void *)lvb, cbLVB );

    report( "VioGetAnsi", VioGetAnsi( &ansi, SESSION_DEFAULT ) );
    printf( "      session interprets escapes: %s\n",
            ansi == ANSI_ON ? "yes" : ansi == ANSI_OFF ? "no" : "unknown" );

    /* What is on this screen before anything is written to it.  If it is the
       text the operator's window shows, the screen behind this process is the
       one the connection is attached to. */
    dumpScreen( "as found", &mode );

    /* Write the marker on the last row -- index mode.row - 1 -- read it back,
       hold it up, and put the row back. */
    lastRow = (USHORT)( mode.row - 1 );
    width = (int)mode.col;
    if( width > ( SCRATCH / 2 ) )
        width = SCRATCH / 2;
    cb = (USHORT)( width * 2 );
    memset( rowSave, 0, sizeof( rowSave ) );
    report( "VioReadCellStr (save)",
            VioReadCellStr( (PCH)rowSave, &cb, lastRow, 0, SESSION_DEFAULT ) );
    printf( "      read back %u bytes of the row it is about to overwrite\n", cb );

    for( i = 0; i + 1 < sizeof( MARKER ); ++i )
    {
        marker[ i * 2 ]     = (UCHAR)MARKER[ i ];
        marker[ i * 2 + 1 ] = MARKER_ATTR;
    }
    report( "VioWrtCellStr",
            VioWrtCellStr( (PCH)marker, (USHORT)( ( sizeof( MARKER ) - 1 ) * 2 ),
                           lastRow, 0, SESSION_DEFAULT ) );
    fflush( stdout );

    cb = (USHORT)( sizeof( readBack ) );
    memset( readBack, 0, sizeof( readBack ) );
    report( "VioReadCellStr (check)",
            VioReadCellStr( (PCH)readBack, &cb, lastRow, 0, SESSION_DEFAULT ) );
    {
        /* Cells are character and attribute interleaved; pull the characters out
           if the text is to be printed. */
        size_t  n = sizeof( MARKER ) - 1;
        for( i = 0; i < n; ++i )
            printable[ i ] = (char)readBack[ i * 2 ];
        printable[ n ] = 0;
        printf( "      read back: \"%s\"\n", printable );
    }

    printf( "  holding the marker for %d seconds: look at the machine's screen\n",
            HOLD_MS / 1000 );
    printf( "  and at the telnet window and see where the marker turned up\n" );
    fflush( stdout );
    dumpScreen( "with the marker", &mode );
    {
        /* The attribute bytes of that row: what came back is what a back end
           would have to write to get the colours it wants. */
        USHORT  attrCb = (USHORT)( width * 2 );
        memset( readBack, 0, sizeof( readBack ) );
        if( VioReadCellStr( (PCH)readBack, &attrCb, lastRow, 0,
                            SESSION_DEFAULT ) == NO_ERROR )
        {
            printf( "      attrs on the marker row:" );
            for( i = 0; i < 12; ++i )
                printf( " %02X", readBack[ i * 2 + 1 ] );
            printf( "\n" );
        }
    }
    DosSleep( HOLD_MS );

    cb = (USHORT)( width * 2 );
    report( "VioWrtCellStr (restore)",
            VioWrtCellStr( (PCH)rowSave, cb, lastRow, 0, SESSION_DEFAULT ) );
    report( "VioSetCurPos (restore)",
            VioSetCurPos( row, col, SESSION_DEFAULT ) );

    memset( &key, 0, sizeof( key ) );
    rc = KbdPeek( &key, SESSION_DEFAULT );
    if( rc == NO_ERROR )
        printf( "  KbdPeek                  -> rc=0, a key is pending: ascii 0x%02X"
                " scan 0x%02X\n", key.chChar, key.chScan );
    else
        printf( "  KbdPeek                  -> rc=%u (rc=0 would mean a key waits)\n",
                (unsigned)rc );

    fflush( stdout );
    return 0;
}
