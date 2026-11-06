// PM capture probe: the questions the OS/2 screen backend asks, put to
// the live box before any of them gets a shipping implementation.
//
// What this settles, one line of output per question:
//
//   * Can a process get a PM anchor (WinInitialize) -- and what does
//     that answer from an IFS session (telnet) versus the box's own
//     display?  (The PM rule says the answer differs; the probe says
//     by how much.)
//   * Does top-level enumeration hang off WinQueryWindow's QW_TOP plus
//     QW_NEXT -- the relationship list in both headers on hand has no
//     QW_CHILD, which is either a trap for the integrator or a limit of
//     the pre-Warp4 headers, and the probe says which by printing real
//     titles.
//   * The Gotcha! recipe end to end: DevOpenDC(OD_MEMORY) ->
//     GpiCreatePS -> GpiQueryDeviceBitmapFormats (does the 16->24 quirk
//     fire here?) -> GpiCreateBitmap -> WinGetPS -> GpiBitBlt ->
//     GpiQueryBitmapBits.  The one question the recipe cannot answer on
//     paper: whether GPI hands back ONE packed 24-bit plane or THREE
//     per-channel ones, and in which byte order.  The probe prints the
//     created bitmap's real planes/bits, dumps the first bytes, and
//     writes a BMP whose colors -- right, swapped, or planar mush --
//     read a verdict.
//
// Run at the box's own display (PM rule: a PM image started from telnet
// hangs that session).  Everything it learns lands in C:\atui\probe_out.txt
// plus probe_full.bmp / probe_win.bmp, which fetch back and look at.
// Watcom's sentinel for a failed DevOpenDC is DEV_ERROR (pmdev.h:136);
// the DC_ERROR spelling belongs to the toolkit headers, not to these.
#define CAPTURE_DC_FAILED( hdc ) ( (hdc) == DEV_ERROR )

// defs.rsp already turns INCL_DOS on for every compile; declaring it a
// second time is only a warning, and warnings are the ones things get
// read for.
#ifndef INCL_DOS
#define INCL_DOS
#endif
#define INCL_GPI
#define INCL_WIN

#include <os2.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/bmpout.h"

// The anchor: WinInitialize(0), the call the PM driver itself makes
// (hardwspm.cpp:3013) and whose link this settles.  (A message queue
// is not needed for queries and captures -- the driver's WinGetMsg
// loop is the application's choice, not the API's.)

namespace {

FILE *g_out = 0;

void say( const char *fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    vfprintf( g_out, fmt, ap );
    va_end( ap );
    std::fflush( g_out );
}

const char *yesno( bool b ) { return b ? "ok" : "FAIL"; }

// The Gotcha! sequence, parameterized by source window.  The whole
// screen passes HWND_DESKTOP with the screen size; a window passes its
// own handle and size and gets its own pixels, wherever the PS finds
// them.
void captureRect( HAB hab, HWND hwnd, LONG w, LONG h,
                  const char *path, const char *tag )
{
    // SAA memory-DC token array, verbatim from the recipe that shipped.
    PSZ pszData[ 4 ] = { "Display", 0, 0, 0 };
    SIZEL sizlPage = { 0, 0 };
    POINTL aptl[ 3 ];
    LONG alData[ 2 ] = { 0, 0 };

    HDC hdcMem = DevOpenDC( hab, OD_MEMORY, "*", 4,
                            (PDEVOPENDATA)pszData, 0 );
    say( "%s: DevOpenDC %s (%ld) lasterr=%lx\n",
         tag, yesno( !CAPTURE_DC_FAILED( hdcMem ) ), (long)hdcMem,
         (unsigned long)WinGetLastError( hab ) );
    if( CAPTURE_DC_FAILED( hdcMem ) )
        return;

    HPS hpsMem = GpiCreatePS( hab, hdcMem, &sizlPage,
                              PU_PELS | GPIA_ASSOC | GPIT_MICRO );
    say( "%s: GpiCreatePS %s\n", tag, yesno( hpsMem != 0 ) );
    if( hpsMem == 0 )
    {
        DevCloseDC( hdcMem );
        return;
    }

    if( GpiQueryDeviceBitmapFormats( hpsMem, 2, alData ) != GPI_ERROR )
        say( "%s: device bitmap formats: planes=%ld bits=%ld\n",
             tag, alData[ 0 ], alData[ 1 ] );
    else
        say( "%s: GpiQueryDeviceBitmapFormats FAIL lasterr=%lx\n",
             tag, (unsigned long)WinGetLastError( hab ) );
    if( alData[ 1 ] == 16 )
        alData[ 1 ] = 24;   // the recipe's quirk: ask 24 where the
                            // device answers 16; verified behavior on
                            // the shipping Gotcha!
    if( alData[ 0 ] <= 0 || alData[ 1 ] <= 0 )
    {
        say( "%s: no usable bitmap format, stopping\n", tag );
        GpiDestroyPS( hpsMem );
        DevCloseDC( hdcMem );
        return;
    }

    BITMAPINFOHEADER2 bi;
    std::memset( &bi, 0, sizeof bi );
    bi.cbFix          = sizeof( BITMAPINFOHEADER2 );
    bi.cx             = w;
    bi.cy             = h;
    bi.cPlanes        = (USHORT)alData[ 0 ];
    bi.cBitCount      = (USHORT)alData[ 1 ];
    bi.ulCompression  = BCA_UNCOMP;
    bi.usUnits        = BRU_METRIC;
    bi.usRecording    = BRA_BOTTOMUP;
    bi.usRendering    = BRH_NOTHALFTONED;
    bi.ulColorEncoding = BCE_RGB;

    HBITMAP hbm = GpiCreateBitmap( hpsMem, &bi, 0L, 0, 0 );
    say( "%s: GpiCreateBitmap(planes=%d bits=%d %ldx%ld) %s\n",
         tag, bi.cPlanes, bi.cBitCount, (long)w, (long)h,
         yesno( hbm != 0 ) );
    if( hbm == 0 )
        say( "%s:   lasterr=%lx\n", tag,
             (unsigned long)WinGetLastError( hab ) );
    if( hbm == 0 )
    {
        GpiDestroyPS( hpsMem );
        DevCloseDC( hdcMem );
        return;
    }

    // What GPI actually built is the question this probe exists for:
    // one packed plane or per-channel planes.
    BITMAPINFOHEADER2 actual;
    std::memset( &actual, 0, sizeof actual );
    actual.cbFix = sizeof actual;
    if( GpiQueryBitmapInfoHeader( hbm, &actual ) )
        say( "%s: created bitmap really is: planes=%d bits=%d\n",
             tag, actual.cPlanes, actual.cBitCount );
    else
    {
        std::memset( &actual, 0, sizeof actual );
        actual.cbFix = sizeof actual;
        actual.cx = w; actual.cy = h;
        actual.cPlanes = bi.cPlanes; actual.cBitCount = bi.cBitCount;
    }

    HBITMAP hbmOld = GpiSetBitmap( hpsMem, hbm );

    HPS hpsSrc = WinGetPS( hwnd );
    say( "%s: WinGetPS %s\n", tag, yesno( hpsSrc != 0 ) );
    if( hpsSrc != 0 )
    {
        aptl[ 0 ].x = 0;   aptl[ 0 ].y = 0;    // dest lower-left
        aptl[ 1 ].x = w;   aptl[ 1 ].y = h;    // dest upper-right
        aptl[ 2 ].x = 0;   aptl[ 2 ].y = 0;    // source lower-left
        LONG blt = GpiBitBlt( hpsMem, hpsSrc, 3, aptl,
                              ROP_SRCCOPY, BBO_IGNORE );
        say( "%s: GpiBitBlt %ld lasterr=%lx\n", tag, blt,
             (unsigned long)WinGetLastError( hab ) );

        // Pull the bits.  Row size follows the created bitmap, planes
        // stacked per the header's own accounting.
        ULONG rowBytes =
            ( (ULONG)actual.cBitCount * (ULONG)actual.cx + 31UL ) / 32UL * 4UL;
        ULONG cbBits =
            rowBytes * (ULONG)actual.cPlanes * (ULONG)actual.cy;
        PBYTE pb = (PBYTE)std::malloc( cbBits );
        if( pb != 0 )
        {
            std::memset( pb, 0, cbBits );
            LONG qrc = GpiQueryBitmapBits( hpsMem, 0, actual.cy, pb,
                                           (PBITMAPINFO2)&actual );
            say( "%s: GpiQueryBitmapBits %s (rc=%ld) bytes=%lu\n", tag,
                 yesno( qrc != GPI_ERROR && qrc != GPI_ALTERROR ),
                 qrc, cbBits );
            say( "%s: first bytes: %02x %02x %02x %02x %02x %02x "
                 "%02x %02x %02x\n", tag,
                 pb[ 0 ], pb[ 1 ], pb[ 2 ], pb[ 3 ], pb[ 4 ], pb[ 5 ],
                 pb[ 6 ], pb[ 7 ], pb[ 8 ] );

            // Convert for writeBmp24 assuming the common answer -- one
            // packed plane, rows bottom-up, blue-first.  If the probe
            // says otherwise the BMP will show it too: swapped channels
            // or vertical flip are both visible as wrongness in the
            // picture itself, which is why the probe writes one.
            if( actual.cPlanes == 1
                && ( actual.cBitCount == 24 || actual.cBitCount == 32 ) )
            {
                const int bpp = actual.cBitCount / 8;
                unsigned char *rgb = (unsigned char *)std::malloc(
                    (size_t)w * (size_t)h * 3 );
                if( rgb != 0 )
                {
                    for( LONG dstY = 0; dstY < h; ++dstY )
                    {
                        // BMP writer wants top-down; bits are bottom-up.
                        const unsigned char *srcRow =
                            pb + (size_t)( h - 1 - dstY ) * rowBytes;
                        unsigned char *dstRow =
                            rgb + (size_t)dstY * (size_t)w * 3;
                        for( LONG x = 0; x < w; ++x )
                        {
                            dstRow[ x * 3 + 0 ] = srcRow[ x * bpp + 2 ];
                            dstRow[ x * 3 + 1 ] = srcRow[ x * bpp + 1 ];
                            dstRow[ x * 3 + 2 ] = srcRow[ x * bpp + 0 ];
                        }
                    }
                    std::string err;
                    bool wrote = atui::platform::writeBmp24(
                        path, rgb, (int)w, (int)h, err );
                    say( "%s: writeBmp24 %s %s\n", tag,
                         wrote ? "ok" : err.c_str(), path );
                    std::free( rgb );
                }
            }
            std::free( pb );
        }
        WinReleasePS( hpsSrc );
    }

    GpiSetBitmap( hpsMem, hbmOld );
    GpiDeleteBitmap( hbm );
    GpiDestroyPS( hpsMem );
    DevCloseDC( hdcMem );
}

} // anonymous namespace

int main()
{
    g_out = std::fopen( "c:\\atui\\probe_out.txt", "w" );
    if( g_out == 0 )
        return 1;
    say( "pmcap probe: start\n" );

    HAB hab = WinInitialize( 0 );
    say( "WinInitialize %s (%p) lasterr=%lx\n", yesno( hab != 0 ), (void *)hab,
         (unsigned long)( hab ? WinGetLastError( hab ) : 0UL ) );
    if( hab == 0 )
    {
        say( "no anchor -- if this session came from telnet, that is "
             "the IFS rule, not the API's answer.  Run at the box's "
             "own display.\n" );
        std::fclose( g_out );
        return 2;
    }

    const LONG cx = WinQuerySysValue( HWND_DESKTOP, SV_CXSCREEN );
    const LONG cy = WinQuerySysValue( HWND_DESKTOP, SV_CYSCREEN );
    say( "screen %ld x %ld\n", cx, cy );

    // Enumerate top-level windows: QW_TOP for the first, QW_NEXT for
    // the rest.  Whether QW_TOP means "first child" (what the
    // integrator needs) or something z-order-flavored is exactly one of
    // the questions -- real titles in the output are the proof.
    HWND hwnd = WinQueryWindow( HWND_DESKTOP, QW_TOP );
    int found = 0;
    HWND biggest = 0;
    LONG bw = 0, bh = 0;
    for( int i = 0; hwnd != 0 && i < 24; ++i )
    {
        CHAR title[ 128 ];
        title[ 0 ] = '\0';
        LONG tl = WinQueryWindowTextLength( hwnd );
        if( tl > 0 )
            WinQueryWindowText( hwnd, (LONG)sizeof title - 1, title );
        title[ sizeof title - 1 ] = '\0';

        // PM apps keep their title on the client window, not the frame
        // (measured: every frame read empty).  QW_TOP was the first
        // child of the desktop above; the same reading walks a frame's
        // children here.  First non-empty child text wins.
        if( title[ 0 ] == '\0' )
        {
            HWND kid = WinQueryWindow( hwnd, QW_TOP );
            for( int k = 0; kid != 0 && k < 12 && title[ 0 ] == '\0'; ++k )
            {
                const LONG kl = WinQueryWindowTextLength( kid );
                if( kl > 0 )
                    WinQueryWindowText( kid, (LONG)sizeof title - 1, title );
                kid = WinQueryWindow( kid, QW_NEXT );
            }
            title[ sizeof title - 1 ] = '\0';
        }

        CHAR cls[ 64 ];
        cls[ 0 ] = '\0';
        WinQueryClassName( hwnd, (LONG)sizeof cls - 1, cls );
        cls[ sizeof cls - 1 ] = '\0';

        SWP swp;
        WinQueryWindowPos( hwnd, &swp );

        PID pid = 0; TID tid = 0;
        WinQueryWindowProcess( hwnd, &pid, &tid );

        say( "win #%d h=%p x=%ld y=%ld cx=%ld cy=%ld vis=%d pid=%lu "
             "class='%s' title='%s'\n",
             i, (void *)hwnd, (long)swp.x, (long)swp.y,
             (long)swp.cx, (long)swp.cy,
             (int)WinIsWindowVisible( hwnd ), (unsigned long)pid,
             cls, title );

        if( hwnd != HWND_DESKTOP && WinIsWindowVisible( hwnd )
            && swp.cx * swp.cy > bw * bh )
        {
            biggest = hwnd; bw = swp.cx; bh = swp.cy;
        }
        ++found;
        hwnd = WinQueryWindow( hwnd, QW_NEXT );
    }
    say( "enumeration: %d window(s)\n", found );

    captureRect( hab, HWND_DESKTOP, cx, cy,
                 "c:\\atui\\probe_full.bmp", "full" );

    // A second, targeted grab: the taskbar strip if enumeration found
    // it -- a real window with real content, exercising the per-window
    // WinGetPS path without waiting on the user to open something.
    hwnd = WinQueryWindow( HWND_DESKTOP, QW_TOP );
    HWND taskbar = 0;
    SWP tbSwp;
    std::memset( &tbSwp, 0, sizeof tbSwp );
    for( int i = 0; hwnd != 0 && i < 32 && taskbar == 0; ++i )
    {
        CHAR cls[ 64 ];
        cls[ 0 ] = '\0';
        WinQueryClassName( hwnd, (LONG)sizeof cls - 1, cls );
        if( std::strcmp( cls, "XWPCenterFrame" ) == 0 )
        {
            taskbar = hwnd;
            WinQueryWindowPos( taskbar, &tbSwp );
        }
        hwnd = WinQueryWindow( hwnd, QW_NEXT );
    }
    if( taskbar != 0 && tbSwp.cx > 0 && tbSwp.cy > 0 )
        captureRect( hab, taskbar, tbSwp.cx, tbSwp.cy,
                     "c:\\atui\\probe_win.bmp", "taskbar" );
    else if( biggest != 0 && bw > 0 && bh > 0 )
        captureRect( hab, biggest, bw, bh,
                     "c:\\atui\\probe_win.bmp", "win" );
    else
        say( "win: no visible window to grab\n" );

    say( "done\n" );
    std::fclose( g_out );
    return 0;
}
