// The desktop seam on OS/2 PM: the capture recipe Gotcha! shipped with,
// measured on the target before a line of this was written.
//
// Compiled under ATUI_HAVE_OS2_PM only -- the PM image's own mark, set
// on the single compile line build.cmd gives this file (obj\pm), with
// screen_none.obj dropped from the pmexe link so exactly one backend
// answers.  The VIO build never sees a word of it beyond an empty
// object, and a box without PM never runs it.
//
// What the probe (tools/os2/probe_pmcap.cpp) settled on this eCS box,
// and what this file therefore assumes:
//
//   * WinInitialize(0) returns an anchor -- so the capture path works
//     from any process PM will host it in, the driver's own anchor
//     included (a second WinInitialize is documented to return the
//     same one).
//   * DevOpenDC(OD_MEMORY, "Display") -> GpiCreatePS(GPIA_ASSOC) ->
//     GpiQueryDeviceBitmapFormats -> GpiCreateBitmap -> WinGetPS ->
//     GpiBitBlt(SRCCOPY) -> GpiQueryBitmapBits, end to end, rc clean.
//     The device answered one packed plane at 32 bits; the 16->24
//     quirk the recipe papers over stays in, cheap, because other
//     display drivers answer it.
//   * The source PS is HWND_DESKTOP, and only the source rectangle
//     moves.  A later round of ground truth (2026-11-07, the drive
//     probe against a live E plus the always-on taskbar) found
//     WinGetPS(window) blitting pure black with rc clean -- success
//     that captures nothing -- while the desktop PS carried the real
//     257-color screen.  rc clean is not the same as pixels right,
//     and the ground-truth pass is what tells them apart.
//   * GpiQueryBitmapBits hands rows bottom-up, bytes B-G-R-(-pad):
//     the flip to the BMP writer's top-down order and the channel swap
//     are both here, and nothing else is between the box and the file.
//   * Window titles live on the frame's client child, not on the frame
//     (every frame read empty); class names come from
//     WinQueryClassName(HWND) -- the toolkit spells the argument a
//     window handle, and the box agrees.
//   * No global keyboard-focus query exists in the headers this port
//     links (WinGetFocus is Warp 4 and later; the Watcom set predates
//     it), so PM listings carry no ACTIVE marker.  Minimized windows
//     read invisible and are left out of the listing rather than
//     marked, since the thing they would be marked for -- "it exists,
//     nothing shows" -- is what the filter says by absence.
//
// Coordinates: PM counts y from the bottom of the screen, and the seam
// has always meant top-left.  The conversion happens here, once, on the
// way out; the tools and the transcript above never see PM's origin.
#include "platform/screen.h"
#include "platform/bmpout.h"

#if defined( ATUI_HAVE_OS2_PM )

#define INCL_GPI
#define INCL_WIN

#include <os2.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace atui {
namespace platform {

namespace {

// A failed DevOpenDC answers DEV_ERROR in Watcom's headers (measured;
// the toolkit's DC_ERROR spelling is not there).
#define PM_DC_FAILED( hdc ) ( (hdc) == DEV_ERROR )

// The anchor, once per process.  WinInitialize is documented to hand
// back the same anchor on repeat calls, so the PM driver having taken
// one at startup is no obstacle here.
HAB anchor()
{
    static HAB hab = WinInitialize( 0 );
    return hab;
}

std::string handleOf( HWND hwnd )
{
    char buf[ 24 ];
    std::snprintf( buf, sizeof buf, "0x%lx", (unsigned long)(ULONG)hwnd );
    return std::string( buf );
}

HWND handleTo( const std::string &handle, bool &ok )
{
    ok = false;
    const char *s = handle.c_str();
    if( s[ 0 ] == '0' && ( s[ 1 ] == 'x' || s[ 1 ] == 'X' ) )
        s += 2;
    char *end = 0;
    const unsigned long v = std::strtoul( s, &end, 16 );
    if( end == s || *end != '\0' )
        return 0;
    ok = true;
    return (HWND)(ULONG)v;
}

// Window text: the frame first, then -- measured on the box -- the
// frame's children, because that is where the title actually lives.
std::string windowText( HWND hwnd )
{
    const LONG tl = WinQueryWindowTextLength( hwnd );
    if( tl > 0 )
    {
        CHAR buf[ 128 ];
        if( WinQueryWindowText( hwnd, (LONG)sizeof buf - 1, buf ) > 0 )
        {
            buf[ sizeof buf - 1 ] = '\0';
            return std::string( buf );
        }
    }
    HWND kid = WinQueryWindow( hwnd, QW_TOP );
    for( int i = 0; kid != 0 && i < 12; ++i )
    {
        const LONG kl = WinQueryWindowTextLength( kid );
        if( kl > 0 )
        {
            CHAR buf[ 128 ];
            if( WinQueryWindowText( kid, (LONG)sizeof buf - 1, buf ) > 0 )
            {
                buf[ sizeof buf - 1 ] = '\0';
                return std::string( buf );
            }
        }
        kid = WinQueryWindow( kid, QW_NEXT );
    }
    return std::string();
}

std::string windowClass( HWND hwnd )
{
    CHAR buf[ 64 ];
    buf[ 0 ] = '\0';
    if( WinQueryClassName( hwnd, (LONG)sizeof buf - 1, buf ) > 0 )
    {
        buf[ sizeof buf - 1 ] = '\0';
        return std::string( buf );
    }
    return std::string();
}

bool grabScreenBits( LONG sx, LONG sy, LONG w, LONG h,
                     std::vector<unsigned char> &rgb,
                     int &outW, int &outH, std::string &err )
{
    const HAB hab = anchor();
    if( hab == 0 )
    {
        err = "no PM anchor (WinInitialize failed)";
        return false;
    }

    PSZ pszData[ 4 ] = { "Display", 0, 0, 0 };
    SIZEL sizlPage = { 0, 0 };
    POINTL aptl[ 3 ];
    LONG alData[ 2 ] = { 0, 0 };

    HDC hdcMem = DevOpenDC( hab, OD_MEMORY, "*", 4,
                            (PDEVOPENDATA)pszData, 0 );
    if( PM_DC_FAILED( hdcMem ) )
    {
        err = "DevOpenDC(OD_MEMORY) failed";
        return false;
    }

    HPS hpsMem = GpiCreatePS( hab, hdcMem, &sizlPage,
                              PU_PELS | GPIA_ASSOC | GPIT_MICRO );
    if( hpsMem == 0 )
    {
        DevCloseDC( hdcMem );
        err = "GpiCreatePS failed on the memory DC";
        return false;
    }

    if( GpiQueryDeviceBitmapFormats( hpsMem, 2, alData ) != GPI_ERROR )
    {
        if( alData[ 1 ] == 16 )
            alData[ 1 ] = 24;   // the recipe's quirk: devices that
                                // answer 16 want 24 asked for (Gotcha!)
    }
    if( alData[ 0 ] <= 0 || alData[ 1 ] <= 0 )
        alData[ 0 ] = 1, alData[ 1 ] = 32;   // the box's own answer

    BITMAPINFOHEADER2 bi;
    std::memset( &bi, 0, sizeof bi );
    bi.cbFix           = sizeof( BITMAPINFOHEADER2 );
    bi.cx              = w;
    bi.cy              = h;
    bi.cPlanes         = (USHORT)alData[ 0 ];
    bi.cBitCount       = (USHORT)alData[ 1 ];
    bi.ulCompression   = BCA_UNCOMP;
    bi.usUnits         = BRU_METRIC;
    bi.usRecording     = BRA_BOTTOMUP;
    bi.usRendering     = BRH_NOTHALFTONED;
    bi.ulColorEncoding = BCE_RGB;

    HBITMAP hbm = GpiCreateBitmap( hpsMem, &bi, 0L, 0, 0 );
    if( hbm == 0 )
    {
        GpiDestroyPS( hpsMem );
        DevCloseDC( hdcMem );
        err = "GpiCreateBitmap failed for a memory bitmap";
        return false;
    }

    BITMAPINFOHEADER2 actual;
    std::memset( &actual, 0, sizeof actual );
    actual.cbFix = sizeof actual;
    if( !GpiQueryBitmapInfoHeader( hbm, &actual ) )
    {
        actual.cx = w;
        actual.cy = h;
        actual.cPlanes = bi.cPlanes;
        actual.cBitCount = bi.cBitCount;
    }

    bool ok = false;
    if( actual.cPlanes != 1
        || ( actual.cBitCount != 24 && actual.cBitCount != 32 ) )
    {
        char dbuf[ 48 ];
        std::snprintf( dbuf, sizeof dbuf,
                       "%d plane(s) at %d bits",
                       (int)actual.cPlanes, (int)actual.cBitCount );
        err = std::string( "display answered " ) + dbuf
            + "; this backend reads one packed 24/32-bit plane";
    }
    else
    {
        HBITMAP hbmOld = GpiSetBitmap( hpsMem, hbm );
        // The source is the shared screen PS, never WinGetPS(window):
        // measured 2026-11-07 against ground truth, a window's own PS
        // blits pure black on this machine (the always-on taskbar
        // captured as one color), while the desktop PS delivers the
        // real 257-color desktop.  Same lesson X11 taught, opposite
        // answer: the shared framebuffer is the source that works
        // here, and the window contributes only its screen rectangle.
        HPS hpsSrc = WinGetPS( HWND_DESKTOP );
        if( hpsSrc != 0 )
        {
            aptl[ 0 ].x = 0;    aptl[ 0 ].y = 0;
            aptl[ 1 ].x = w;    aptl[ 1 ].y = h;
            aptl[ 2 ].x = sx;   aptl[ 2 ].y = sy;
            const LONG blt = GpiBitBlt( hpsMem, hpsSrc, 3, aptl,
                                        ROP_SRCCOPY, BBO_IGNORE );
            if( blt != GPI_ERROR )
            {
                const ULONG rowBytes =
                    ( (ULONG)actual.cBitCount * (ULONG)actual.cx + 31UL )
                    / 32UL * 4UL;
                const ULONG cbBits = rowBytes * (ULONG)actual.cy;
                std::vector<unsigned char> bits( cbBits, 0 );
                const LONG qrc = GpiQueryBitmapBits(
                    hpsMem, 0, actual.cy, &bits[ 0 ],
                    (PBITMAPINFO2)&actual );
                if( qrc != GPI_ERROR && qrc != GPI_ALTERROR )
                {
                    // Bottom-up BGR(-pad) in, top-down RGB out.
                    const int bpp = actual.cBitCount / 8;
                    rgb.resize( (size_t)w * (size_t)h * 3 );
                    for( LONG y = 0; y < h; ++y )
                    {
                        const unsigned char *srcRow =
                            &bits[ 0 ]
                            + (size_t)( h - 1 - y ) * rowBytes;
                        unsigned char *dstRow =
                            &rgb[ 0 ] + (size_t)y * (size_t)w * 3;
                        for( LONG x = 0; x < w; ++x )
                        {
                            dstRow[ x * 3 + 0 ] = srcRow[ x * bpp + 2 ];
                            dstRow[ x * 3 + 1 ] = srcRow[ x * bpp + 1 ];
                            dstRow[ x * 3 + 2 ] = srcRow[ x * bpp + 0 ];
                        }
                    }
                    outW = (int)w;
                    outH = (int)h;
                    ok = true;
                }
                else
                    err = "GpiQueryBitmapBits failed";
            }
            else
                err = "GpiBitBlt from the screen failed";
            WinReleasePS( hpsSrc );
        }
        else
            err = "WinGetPS(HWND_DESKTOP) failed (PM said no)";
        GpiSetBitmap( hpsMem, hbmOld );
    }

    GpiDeleteBitmap( hbm );
    GpiDestroyPS( hpsMem );
    DevCloseDC( hdcMem );
    return ok;
}

void fillWindow( HWND hwnd, WindowInfo &info )
{
    SWP swp;
    WinQueryWindowPos( hwnd, &swp );
    info.handle = handleOf( hwnd );
    info.x = (int)swp.x;
    info.y = (int)swp.y;
    info.w = (int)swp.cx;
    info.h = (int)swp.cy;
    // PM counts y from the bottom; the seam means top-left, and top is
    // y + height down there.
    info.y = (int)( WinQuerySysValue( HWND_DESKTOP, SV_CYSCREEN )
                    - swp.y - swp.cy );
    info.title = windowText( hwnd );
    info.cls = windowClass( hwnd );
    info.active = false;    // no global focus query pre-Warp4 (see top)
    info.iconic = false;
}

void appendChildren( HWND hwnd, std::vector<WindowInfo> &into )
{
    HWND kid = WinQueryWindow( hwnd, QW_TOP );
    for( int i = 0; kid != 0 && i < 32; ++i )
    {
        SWP swp;
        WinQueryWindowPos( kid, &swp );
        if( swp.cx > 0 && swp.cy > 0 )
        {
            WindowInfo info;
            fillWindow( kid, info );
            into.push_back( info );
        }
        kid = WinQueryWindow( kid, QW_NEXT );
    }
}

} // anonymous namespace

bool captureAvailable()
{
    return anchor() != 0;
}

bool windowsAvailable()
{
    return anchor() != 0;
}

CaptureResult captureScreen( const std::string &path )
{
    CaptureResult res;
    const LONG w = WinQuerySysValue( HWND_DESKTOP, SV_CXSCREEN );
    const LONG h = WinQuerySysValue( HWND_DESKTOP, SV_CYSCREEN );
    if( w <= 0 || h <= 0 )
    {
        res.error = "PM reports no screen to capture";
        return res;
    }

    std::vector<unsigned char> rgb;
    int bw = 0, bh = 0;
    std::string err;
    if( !grabScreenBits( 0, 0, w, h, rgb, bw, bh, err ) )
    {
        res.error = err;
        return res;
    }
    if( !writeBmp24( path, &rgb[ 0 ], bw, bh, err ) )
    {
        res.error = err;
        return res;
    }
    res.ok = true;
    res.path = path;
    res.width = bw;
    res.height = bh;
    return res;
}

CaptureResult captureWindow( const std::string &handle,
                             const std::string &path )
{
    CaptureResult res;
    bool parses = false;
    const HWND hwnd = handleTo( handle, parses );
    if( !parses || !WinIsWindow( anchor(), hwnd ) )
    {
        res.error = "no window " + handle + " (closed, or never was)";
        return res;
    }
    if( !WinIsWindowVisible( hwnd ) )
    {
        res.error = "window " + handle
                  + " is not visible (minimized windows hold nothing "
                    "to capture here)";
        return res;
    }

    SWP swp;
    WinQueryWindowPos( hwnd, &swp );
    // The rectangle gets clamped to the screen: the source of pixels
    // is the framebuffer, so whatever hangs off the edge has none.
    // Parked windows sit at -32000, and the clamp answers them with
    // the honest complaint that nothing of them shows.
    const LONG sxMax = WinQuerySysValue( HWND_DESKTOP, SV_CXSCREEN );
    const LONG syMax = WinQuerySysValue( HWND_DESKTOP, SV_CYSCREEN );
    LONG x0 = swp.x < 0 ? 0 : swp.x;
    LONG y0 = swp.y < 0 ? 0 : swp.y;
    LONG x1 = swp.x + swp.cx > sxMax ? sxMax : swp.x + swp.cx;
    LONG y1 = swp.y + swp.cy > syMax ? syMax : swp.y + swp.cy;
    if( x1 - x0 <= 0 || y1 - y0 <= 0 )
    {
        res.error = "window " + handle
                  + " shows nothing on this screen (off the edge, or "
                    "on another virtual desktop)";
        return res;
    }

    std::vector<unsigned char> rgb;
    int bw = 0, bh = 0;
    std::string err;
    if( !grabScreenBits( x0, y0, x1 - x0, y1 - y0, rgb, bw, bh, err ) )
    {
        res.error = err;
        return res;
    }
    if( !writeBmp24( path, &rgb[ 0 ], bw, bh, err ) )
    {
        res.error = err;
        return res;
    }
    res.ok = true;
    res.path = path;
    res.width = bw;
    res.height = bh;
    return res;
}

bool listWindows( std::vector<WindowInfo> &out, bool children )
{
    out.clear();
    if( anchor() == 0 )
        return false;

    // QW_TOP answered "the first child" of the desktop on the box, and
    // QW_NEXT walks the rest -- the relationship list in these headers
    // has no QW_CHILD at all, which the probe confirmed is not needed.
    HWND hwnd = WinQueryWindow( HWND_DESKTOP, QW_TOP );
    for( int i = 0; hwnd != 0 && i < 64; ++i )
    {
        SWP swp;
        WinQueryWindowPos( hwnd, &swp );
        if( WinIsWindowVisible( hwnd )
            && swp.cx > 0 && swp.cy > 0
            && swp.x + swp.cx > 0 && swp.y + swp.cy > 0 )
        {
            WindowInfo info;
            fillWindow( hwnd, info );
            if( children )
                appendChildren( hwnd, info.children );
            out.push_back( info );
        }
        hwnd = WinQueryWindow( hwnd, QW_NEXT );
    }
    return true;
}

} // namespace atui::platform
} // namespace atui

#endif // ATUI_HAVE_OS2_PM
