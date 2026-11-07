// PM drive probe: the hands against a foreign, real application.
//
// The arena probe (probe_pmhands) pokes windows it owns itself; this
// one pokes someone else's -- E, the PM editor that ships with every
// OS/2 -- because the shipping tools will drive foreign windows too,
// and only a stranger's controls prove what a stranger's controls do.
//
// What this settles, in the order the tool will need them:
//
//   * Launch by session spawn (DosStartSession).  From the telnet
//     anchor only the background form is accepted (measured: every
//     foreground request answers 457, background answers 0) -- and a
//     window from that background spawn DOES show on the visible
//     desktop (measured by eye, 2026-11-07).  So launch works even
//     from here; the tool's session (atuipm, PM, on the desktop)
//     should do even better.
//   * Find it without a PID (the background spawn hands back pid=0):
//     diff the top-level enumeration around the launch -- the new
//     window IS the program.  Class names and children print, with
//     QWS_ID read off controls E itself created -- the control-id
//     round-trip, measured on a stranger instead of on a synthetic.
//   * Set text where text is visible: WinSetWindowText on the frame.
//     The title is window text, so this one verifies itself from
//     across processes.
//   * Keys: WM_CHAR sent to the largest child (E edits in a WRAPWIN,
//     which has no public read-its-content message) -- the proof of
//     typing is therefore pixels, and the probe captures the window
//     right after, same WinGetPS/GpiBitBlt recipe that shipped for
//     capture, into drive_shot.bmp.
//   * Close: WM_CLOSE at the frame.  E has unsaved changes by then,
//     so a real application answers a close request with a real
//     modal dialog -- finding that dialog, then clicking its "No"
//     button (WM_COMMAND with the dialog's own control id at its
//     owner, the notification filled with the CN_SELECTED candidate
//     0x0100) is the dismiss test.  If the dialog vanishes and then
//     E vanishes, clicks work on real controls, and so would Cancel.
//
// Everything is observable from telnet except the pixels -- and the
// pixels come back as a BMP.  Answers land in C:\atui\drive_out.txt.
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

namespace {

FILE *g_out = 0;
HAB g_hab = 0;

void say( const char *fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    vfprintf( g_out, fmt, ap );
    va_end( ap );
    std::fflush( g_out );
}

const char *yesno( bool b ) { return b ? "ok" : "FAIL"; }

void pumpMs( unsigned ms )
{
    for( unsigned waited = 0; waited < ms; waited += 50 )
    {
        QMSG qm;
        while( WinPeekMsg( g_hab, &qm, 0, 0, 0, PM_REMOVE ) )
            WinDispatchMsg( g_hab, &qm );
        DosSleep( 50 );
    }
}

struct TopList
{
    HWND h[ 96 ];
    int n;
};

void snapshotTops( TopList &t )
{
    t.n = 0;
    HWND w = WinQueryWindow( HWND_DESKTOP, QW_TOP );
    for( int i = 0; w != 0 && i < 96; ++i )
    {
        t.h[ t.n++ ] = w;
        w = WinQueryWindow( w, QW_NEXT );
    }
}

bool isTopped( const TopList &t, HWND h )
{
    for( int i = 0; i < t.n; ++i )
        if( t.h[ i ] == h )
            return true;
    return false;
}

void describeWindow( const char *tag, HWND hwnd )
{
    CHAR cls[ 64 ];
    cls[ 0 ] = '\0';
    WinQueryClassName( hwnd, (LONG)sizeof cls - 1, cls );
    CHAR title[ 128 ];
    title[ 0 ] = '\0';
    if( WinQueryWindowTextLength( hwnd ) > 0 )
        WinQueryWindowText( hwnd, (LONG)sizeof title - 1, title );
    SWP swp;
    WinQueryWindowPos( hwnd, &swp );
    PID pid = 0;
    TID tid = 0;
    WinQueryWindowProcess( hwnd, &pid, &tid );
    say( "%s h=%p class='%s' title='%s' x=%ld y=%ld %ldx%ld vis=%d pid=%lu\n",
         tag, (void *)hwnd, cls, title, (long)swp.x, (long)swp.y,
         (long)swp.cx, (long)swp.cy, (int)WinIsWindowVisible( hwnd ),
         (unsigned long)pid );
}

void dumpChildren( HWND frame )
{
    if( frame == 0 )
        return;
    int kids = 0;
    HWND c = WinQueryWindow( frame, QW_TOP );
    for( int i = 0; c != 0 && i < 16; ++i )
    {
        CHAR cls[ 48 ];
        cls[ 0 ] = '\0';
        WinQueryClassName( c, (LONG)sizeof cls - 1, cls );
        CHAR txt[ 64 ];
        txt[ 0 ] = '\0';
        if( WinQueryWindowTextLength( c ) > 0 )
            WinQueryWindowText( c, (LONG)sizeof txt - 1, txt );
        SWP swp;
        WinQueryWindowPos( c, &swp );
        say( "  child %d: h=%p class='%s' text='%s' id=%x %ldx%ld\n",
             kids, (void *)c, cls, txt,
             (unsigned)WinQueryWindowUShort( c, QWS_ID ),
             (long)swp.cx, (long)swp.cy );
        ++kids;
        c = WinQueryWindow( c, QW_NEXT );
    }
    say( "children: %d\n", kids );
}

// The largest child is the editing area; the small ones are scroll
// bars and the like, which swallow keys into nowhere.
HWND biggestChild( HWND frame )
{
    HWND best = 0;
    LONG bestArea = 0;
    HWND c = WinQueryWindow( frame, QW_TOP );
    for( int i = 0; c != 0 && i < 16; ++i )
    {
        SWP swp;
        WinQueryWindowPos( c, &swp );
        const LONG area = swp.cx * swp.cy;
        if( area > bestArea )
        {
            bestArea = area;
            best = c;
        }
        c = WinQueryWindow( c, QW_NEXT );
    }
    return best;
}

// Capture a screen rectangle as a BMP -- the Gotcha! GPI chain with
// the ONE source that measures right on this box (2026-11-07): the
// desktop PS.  WinGetPS(window) blits pure black here with rc clean,
// found by pointing it at an always-visible taskbar and getting one
// color back.  Coordinates are PM's, bottom-origin, same on both sides.
bool captureScreenRectBmp( LONG sx, LONG sy, LONG w, LONG h,
                           const char *path )
{
    PSZ pszData[ 4 ] = { "Display", 0, 0, 0 };
    SIZEL sizlPage = { 0, 0 };
    POINTL aptl[ 3 ];
    LONG alData[ 2 ] = { 0, 0 };

    HDC hdcMem = DevOpenDC( g_hab, OD_MEMORY, "*", 4,
                            (PDEVOPENDATA)pszData, 0 );
    if( hdcMem == DEV_ERROR )
    {
        say( "capture: DevOpenDC FAIL\n" );
        return false;
    }
    HPS hpsMem = GpiCreatePS( g_hab, hdcMem, &sizlPage,
                              PU_PELS | GPIA_ASSOC | GPIT_MICRO );
    if( hpsMem == 0 )
    {
        say( "capture: GpiCreatePS FAIL\n" );
        DevCloseDC( hdcMem );
        return false;
    }
    if( GpiQueryDeviceBitmapFormats( hpsMem, 2, alData ) == GPI_ERROR )
        alData[ 0 ] = 1, alData[ 1 ] = 24;
    if( alData[ 1 ] == 16 )
        alData[ 1 ] = 24;

    BITMAPINFOHEADER2 bi;
    std::memset( &bi, 0, sizeof bi );
    bi.cbFix = sizeof( BITMAPINFOHEADER2 );
    bi.cx = w;
    bi.cy = h;
    bi.cPlanes = (USHORT)alData[ 0 ];
    bi.cBitCount = (USHORT)alData[ 1 ];
    bi.ulCompression = BCA_UNCOMP;
    bi.usUnits = BRU_METRIC;
    bi.usRecording = BRA_BOTTOMUP;
    bi.usRendering = BRH_NOTHALFTONED;
    bi.ulColorEncoding = BCE_RGB;
    HBITMAP hbm = GpiCreateBitmap( hpsMem, &bi, 0L, 0, 0 );
    if( hbm == 0 )
    {
        say( "capture: GpiCreateBitmap FAIL\n" );
        GpiDestroyPS( hpsMem );
        DevCloseDC( hdcMem );
        return false;
    }

    bool ok = false;
    HBITMAP hbmOld = GpiSetBitmap( hpsMem, hbm );
    HPS hpsSrc = WinGetPS( HWND_DESKTOP );
    if( hpsSrc != 0 )
    {
        aptl[ 0 ].x = 0;  aptl[ 0 ].y = 0;
        aptl[ 1 ].x = w;  aptl[ 1 ].y = h;
        aptl[ 2 ].x = sx; aptl[ 2 ].y = sy;
        LONG blt = GpiBitBlt( hpsMem, hpsSrc, 3, aptl,
                              ROP_SRCCOPY, BBO_IGNORE );
        WinReleasePS( hpsSrc );
        say( "capture: GpiBitBlt rc=%ld\n", blt );
        const ULONG rowBytes =
            ( (ULONG)bi.cBitCount * (ULONG)w + 31UL ) / 32UL * 4UL;
        const ULONG cbBits = rowBytes * (ULONG)h;
        PBYTE pb = (PBYTE)std::malloc( cbBits );
        if( pb != 0 )
        {
            std::memset( pb, 0, cbBits );
            LONG qrc = GpiQueryBitmapBits( hpsMem, 0, h, pb,
                                           (PBITMAPINFO2)&bi );
            say( "capture: GpiQueryBitmapBits rc=%ld\n", qrc );
            if( qrc != GPI_ERROR && qrc != GPI_ALTERROR )
            {
                const int bpp = bi.cBitCount / 8;
                unsigned char *rgb = (unsigned char *)std::malloc(
                    (size_t)w * (size_t)h * 3 );
                if( rgb != 0 )
                {
                    for( LONG dstY = 0; dstY < h; ++dstY )
                    {
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
                    ok = atui::platform::writeBmp24( path, rgb,
                                                     (int)w, (int)h, err );
                    if( !ok )
                        say( "capture: write %s\n", err.c_str() );
                    std::free( rgb );
                }
            }
            std::free( pb );
        }
    }
    else
        say( "capture: WinGetPS FAIL\n" );
    GpiSetBitmap( hpsMem, hbmOld );
    GpiDeleteBitmap( hbm );
    GpiDestroyPS( hpsMem );
    DevCloseDC( hdcMem );
    return ok;
}

// Wait for a NEW top-level window (post-launch), then return it.
HWND waitForNew( const TopList &before, unsigned timeoutMs )
{
    for( unsigned waited = 0; waited < timeoutMs; waited += 400 )
    {
        DosSleep( 400 );
        pumpMs( 50 );
        HWND w = WinQueryWindow( HWND_DESKTOP, QW_TOP );
        for( int i = 0; w != 0 && i < 96; ++i )
        {
            if( !isTopped( before, w ) )
            {
                SWP swp;
                WinQueryWindowPos( w, &swp );
                if( swp.cx > 8 && swp.cy > 8 )   // ignore transient slivers
                    return w;
            }
            w = WinQueryWindow( w, QW_NEXT );
        }
    }
    return 0;
}

// Find the newest dialog (a new top-level again) after WM_CLOSE;
// E's "save changes?" is a real modal box from E's own code.
HWND waitForDialog( const TopList &before, unsigned timeoutMs )
{
    for( unsigned waited = 0; waited < timeoutMs; waited += 300 )
    {
        DosSleep( 300 );
        pumpMs( 50 );
        HWND w = WinQueryWindow( HWND_DESKTOP, QW_TOP );
        for( int i = 0; w != 0 && i < 96; ++i )
        {
            if( !isTopped( before, w ) )
            {
                CHAR cls[ 48 ];
                cls[ 0 ] = '\0';
                WinQueryClassName( w, (LONG)sizeof cls - 1, cls );
                SWP swp;
                WinQueryWindowPos( w, &swp );
                if( swp.cx > 40 && swp.cy > 30 )
                    return w;
            }
            w = WinQueryWindow( w, QW_NEXT );
        }
    }
    return 0;
}

// Find the running atuipm: its client window carries the registered
// class the PM driver gives it, and the prompt inside is a WC_MLE --
// a control that will ANSWER typing (MLM_QUERYTEXTLENGTH moves when
// characters land), which is the programmatic proof no screenshot can
// provide on its own.
void driveAtui()
{
    // The chat client nests WPS frame -> AtuiGroup -> AtuiApp, so a
    // top-level-only search (the first draft's mistake, and the same
    // reason the cross-process read never found anyone) misses it.
    // Walk two levels of children with case-off matching.
    HWND app = 0;
    HWND w = WinQueryWindow( HWND_DESKTOP, QW_TOP );
    for( int i = 0; w != 0 && i < 96 && app == 0; ++i )
    {
        HWND c = WinQueryWindow( w, QW_TOP );
        for( int k = 0; c != 0 && k < 16 && app == 0; ++k )
        {
            if( c != w )
            {
                CHAR cls[ 48 ];
                cls[ 0 ] = '\0';
                WinQueryClassName( c, (LONG)sizeof cls - 1, cls );
                if( stricmp( cls, "AtuiApp" ) == 0 )
                    app = c;
                // one level deeper still, in case the group nests
                HWND g = WinQueryWindow( c, QW_TOP );
                for( int m = 0; g != 0 && m < 16 && app == 0; ++m )
                {
                    CHAR gcls[ 48 ];
                    gcls[ 0 ] = '\0';
                    WinQueryClassName( g, (LONG)sizeof gcls - 1, gcls );
                    if( stricmp( gcls, "AtuiApp" ) == 0 )
                        app = g;
                    g = WinQueryWindow( g, QW_NEXT );
                }
            }
            c = WinQueryWindow( c, QW_NEXT );
        }
        w = WinQueryWindow( w, QW_NEXT );
    }
    if( app == 0 )
    {
        say( "atui: no AtuiApp window -- is atuipm up at the display? "
             "(a new build of atuipm must be running for the search "
             "classes to exist at all)\n" );
        return;
    }
    describeWindow( "atui client:", app );
    dumpChildren( app );
    dumpChildren( WinQueryWindow( app, QW_PARENT ) );

    // The MLE: the driver's prompt child.  A system class answers
    // WinQueryClassName as "#n", and WC_MLE -- 0xffff000a in Watcom's
    // pmwin.h:131 -- reports as "#10" (measured against the running
    // chat client; "MLE" kept in the test for machines that answer
    // in words).
    HWND mle = 0;
    HWND c = WinQueryWindow( app, QW_TOP );
    for( int i = 0; c != 0 && i < 12 && mle == 0; ++i )
    {
        CHAR cls[ 48 ];
        cls[ 0 ] = '\0';
        WinQueryClassName( c, (LONG)sizeof cls - 1, cls );
        if( std::strcmp( cls, "MLE" ) == 0
            || std::strcmp( cls, "#10" ) == 0 )
            mle = c;
        c = WinQueryWindow( c, QW_NEXT );
    }
    if( mle == 0 )
    {
        // One level up: the prompt might be a sibling under the group.
        HWND grp = WinQueryWindow( app, QW_PARENT );
        c = WinQueryWindow( grp, QW_TOP );
        for( int i = 0; c != 0 && i < 12 && mle == 0; ++i )
        {
            CHAR cls[ 48 ];
            cls[ 0 ] = '\0';
            WinQueryClassName( c, (LONG)sizeof cls - 1, cls );
            if( std::strcmp( cls, "MLE" ) == 0
                || std::strcmp( cls, "#10" ) == 0 )
                mle = c;
            c = WinQueryWindow( c, QW_NEXT );
        }
    }
    if( mle == 0 )
    {
        say( "atui: no MLE child found\n" );
        return;
    }
    // The one legal cross-process text read: window text via the
    // marshaled query API.  Whether an MLE mirrors typed text into
    // its window-text object is a guess -- but it is a cheap one,
    // and last round's evidence (letters A/B/C heard by the pump)
    // died behind a minimized window before any capture could see
    // them.  "ABC" here means the earlier rounds' keys DID land and
    // only the witness was missing; empty means they truly didn't.
    CHAR ptxt[ 128 ];
    ptxt[ 0 ] = '\0';
    const LONG ptl =
        WinQueryWindowText( mle, (LONG)sizeof ptxt - 1, ptxt );
    say( "atui: prompt window-text = %ld chars '%s'\n",
         (long)ptl, ptxt );
    // Round 2, informed by the chat client's own key log: last run's
    // posted WM_CHARs ALL reached its pump (six kmsg lines, mp2 values
    // matching the posts exactly) and the pump dispatched them to the
    // MLE -- which swallowed every one.  Same window, same queue,
    // same message, no insertion.  The remaining difference between
    // those posts and the user's keystrokes is focus: atuipm was
    // started at this display but nothing has activated it since, and
    // toolkit edit controls are known to guard insertion on holding
    // the real keyboard focus.
    //
    // So: name the focus, activate, and type again -- with pixels as
    // the meter (MLM read-back is out of reach across processes: the
    // export messages move raw pointers PM does not marshal).
    HWND focusNow = WinQueryFocus( HWND_DESKTOP );
    CHAR fcls[ 48 ];
    fcls[ 0 ] = '\0';
    if( focusNow != 0 )
        WinQueryClassName( focusNow, (LONG)sizeof fcls - 1, fcls );
    say( "atui: focus now = %p class '%s' (mle is %p)\n",
         (void *)focusNow, fcls, (void *)mle );

    const BOOL act = WinSetFocus( HWND_DESKTOP, mle );
    say( "atui: WinSetFocus(desktop, mle) rc=%d err=0x%x\n", (int)act,
         (unsigned long)WinGetLastError( g_hab ) );
    pumpMs( 300 );
    focusNow = WinQueryFocus( HWND_DESKTOP );
    say( "atui: focus after activation = %p\n", (void *)focusNow );

    // The witness: the outer frame's rectangle, captured before
    // anything is typed.  Last round this rectangle was showing a
    // file-manager window -- the chat client was minimized -- and
    // every "the keys did not land" verdict from those frames is
    // void: they photographed the wrong window.  The baseline makes
    // that failure mode impossible to fall into twice: if the
    // baseline is not the chat UI, nothing after it means anything.
    HWND grp = WinQueryWindow( app, QW_PARENT );
    HWND shot = grp != 0 ? grp : app;
    HWND outer = WinQueryWindow( shot, QW_PARENT );
    if( outer != 0 && outer != HWND_DESKTOP )
        shot = outer;
    SWP swp;
    WinQueryWindowPos( shot, &swp );
    say( "atui: baseline frame h=%p x=%ld y=%ld %ldx%ld vis=%d -> "
         "drive_atui0.bmp\n", (void *)shot, (long)swp.x, (long)swp.y,
         (long)swp.cx, (long)swp.cy,
         (int)WinIsWindowVisible( shot ) );
    captureScreenRectBmp( swp.x, swp.y, swp.cx, swp.cy,
                          "c:\\atui\\drive_atui0.bmp" );

    // Round 3, letters as labels, plus a control nobody can blame on
    // focus: stuffing the control through the marshaled window-text
    // door (WinSetWindowText with SPBR_TEXT on an MLE) proves pixels
    // can arrive at all.  Focus is now proven reachable (screensaver
    // off, focus sat on the MLE by itself, WinSetFocus(desktop,mle)
    // rc=1), yet two rounds of posted keys typed nothing while the
    // pump's own kmsg log proves they reached it -- so the remaining
    // suspects are the mp1/mp2 encodings, and the driver's
    // transcription of the box's pmwin.h points right at them: WM_CHAR
    // flags live in mp1's low short (KC_CHAR 0x0001, KC_KEYUP 0x0040,
    // shift family 0x0008/0x0010/0x0020) and every earlier post
    // carried mp1=0 -- "not a character" to a control that checks.
    // One letter per encoding, typed in order: the letters that
    // appear spell which encodings work, and their order proves
    // posted/sent interleaving preserves sequence.
    struct Variant { char letter; ULONG flags; int scan; int virt;
                     int mode; const char *tag; };
    const Variant variants[] = {
        { 'A', 0x0000, 0x00, 0x0000, 0, "post mp1=0" },
        { 'B', 0x0001, 0x00, 0x0000, 0, "post KC_CHAR" },
        { 'C', 0x0001, 0x23, 0x0000, 0, "post KC_CHAR+scan(h)" },
        { 'D', 0x0146, 0x00, 0x0000, 0, "post flags 0x146" },
        { 'E', 0x0001, 0x00, 0x0000, 1, "SEND KC_CHAR" },
        { 'F', 0x0146, 0x00, 0x0000, 1, "SEND flags 0x146" },
        { 'G', 0x0001, 0x00, 0x0000, 2, "SEND WM_CHAR 0x1E47" },
    };
    for( int v = 0; v < (int)(sizeof variants / sizeof variants[0]);
         ++v )
    {
        const Variant &x = variants[ v ];
        // mp1: flags in the low short, scan byte fourth (the shape
        // the pump itself reads: CHAR4FROMMP(mp1) for the scan).
        const MPARAM mp1 = MPFROM2SHORT( (SHORT)x.flags, (SHORT)x.scan );
        // mp2: character in the low byte, virtual key in the high
        // short; 0 means "no virtual key", which the pump's page-key
        // tests (0x11/0x12) pass through untouched.  Mode 2 packs
        // the physical shape the driver's raw log saw: scan riding
        // in the byte just above the character (0x1e1c0d for Enter).
        MPARAM mp2;
        if( x.mode == 2 )
            mp2 = MPFROMLONG( ( (ULONG)0x1E << 8 )
                              | (unsigned char)x.letter );
        else
            mp2 = MPFROM2SHORT( (SHORT)x.letter, (SHORT)x.virt );
        const MRESULT r =
            ( x.mode == 0 )
                ? (MRESULT)(ULONG)( WinPostMsg( mle, WM_CHAR, mp1, mp2 )
                                    ? 1 : 0 )
                : WinSendMsg( mle, WM_CHAR, mp1, mp2 );
        say( "atui: typed '%c' via %s (mode %d, r=0x%lx)\n", x.letter,
             x.tag, x.mode, (unsigned long)(ULONG)r );
        pumpMs( 200 );   // keeps the interleaved sends/posts in order
    }
    pumpMs( 800 );

    say( "atui: all variants typed -> drive_atui6.bmp "
         "(read the prompt: the letters that appear name the "
         "accepted encodings)\n" );
    captureScreenRectBmp( swp.x, swp.y, swp.cx, swp.cy,
                          "c:\\atui\\drive_atui6.bmp" );

    // Control experiment: the marshaled door.  WM_SETTEXT is a system
    // message whose pointer parameters PM copies across address
    // spaces legally, and WinSetWindowText asks exactly that.  If
    // THIS paints and the letters did not, the control guards keys
    // but not text-set; if neither paints, the control (or its host)
    // is deaf to outside writers entirely -- different tool design.
    const BOOL swr = WinSetWindowText( mle, (PSZ)"WXYZ" );
    say( "atui: WinSetWindowText(WXYZ) rc=%d err=0x%x\n", (int)swr,
         (unsigned long)WinGetLastError( g_hab ) );
    pumpMs( 600 );
    captureScreenRectBmp( swp.x, swp.y, swp.cx, swp.cy,
                          "c:\\atui\\drive_atui8.bmp" );
    WinSetWindowText( mle, (PSZ)"" );   // clean the house either way
    pumpMs( 400 );

    // Cleanup attempt through the same door: backspace as the pump
    // sees it -- KC_CHAR plus the backspace character.  Whether the
    // strip empties is itself measured; the frame is the witness.
    for( int i = 0; i < 6; ++i )
        WinPostMsg( mle, WM_CHAR, MPFROM2SHORT( 0x0001, 0 ),
                    MPFROM2SHORT( 0x08, 0 ) );
    pumpMs( 800 );
    say( "atui: posted 6 backspaces -> drive_atui7.bmp\n" );
    captureScreenRectBmp( swp.x, swp.y, swp.cx, swp.cy,
                          "c:\\atui\\drive_atui7.bmp" );
}

void clickButtonNamed( HWND hDlg, const char *wanted )
{
    HWND c = WinQueryWindow( hDlg, QW_TOP );
    for( int i = 0; c != 0 && i < 24; ++i )
    {
        CHAR txt[ 48 ];
        txt[ 0 ] = '\0';
        if( WinQueryWindowTextLength( c ) > 0 )
            WinQueryWindowText( c, (LONG)sizeof txt - 1, txt );
        if( txt[ 0 ] != '\0'
            && std::strchr( txt, (int)wanted[ 0 ] ) != 0 )
        {
            const USHORT id = WinQueryWindowUShort( c, QWS_ID );
            HWND owner = WinQueryWindow( c, QW_OWNER );
            say( "click: '%s' id=%x owner=%s -> WM_COMMAND notif 0x0100\n",
                 txt, (unsigned)id, owner != 0 ? "owner" : "self" );
            WinPostMsg( owner != 0 ? owner : hDlg, WM_COMMAND,
                        MPFROMLONG( id ), MPFROM2SHORT( 0x0100, 0 ) );
            return;
        }
        c = WinQueryWindow( c, QW_NEXT );
    }
    say( "click: no child text matching '%s'\n", wanted );
}

} // anonymous namespace

int main( int argc, char **argv )
{
    g_out = std::fopen( "c:\\atui\\drive_out.txt", "w" );
    if( g_out == 0 )
        return 1;
    say( "pmdrive probe: start\n" );

    g_hab = WinInitialize( 0 );
    say( "WinInitialize %s\n", yesno( g_hab != 0 ) );
    if( g_hab == 0 )
    {
        std::fclose( g_out );
        return 2;
    }

    if( argc > 2 && std::strcmp( argv[ 1 ], "--kill" ) == 0 )
    {
        // taskkill on this box is a GUI task list, not a command; the
        // direct API is the way.  A wedged PM app leaves its window
        // object behind until its process actually dies.
        const ULONG pid = (ULONG)strtoul( argv[ 2 ], 0, 10 );
        // The names in the modern docs (KPT_TERM/KPT_NOW) postdate
        // this header set; the classic single option bit (the old
        // KILLPROCESSON, value 1) is what this kernel must accept,
        // and the Warp-era pair is tried first for the same door.
        APIRET rc = DosKillProcess( 0x0001 | 0x0002, pid );
        if( rc != 0 )
            rc = DosKillProcess( 0x0001, pid );
        say( "kill %lu rc=%lu\n", pid, (unsigned long)rc );
        std::fclose( g_out );
        WinTerminate( g_hab );
        return (int)( rc != 0 );
    }

    if( argc > 1 && std::strcmp( argv[ 1 ], "--shot" ) == 0 )
    {
        // Whole-screen look plus the top-window census: which window
        // actually covers what, and whether the chat client is even
        // still there.  Cheap to run from anywhere, and the picture
        // answers questions the per-window queries keep getting wrong.
        HWND w = WinQueryWindow( HWND_DESKTOP, QW_TOP );
        for( int i = 0; w != 0 && i < 64; ++i )
        {
            describeWindow( "top:", w );
            w = WinQueryWindow( w, QW_NEXT );
        }
        SWP dsk;
        WinQueryWindowPos( HWND_DESKTOP, &dsk );
        say( "shot: desktop %ldx%ld -> drive_desktop.bmp\n",
             (long)dsk.cx, (long)dsk.cy );
        captureScreenRectBmp( 0, 0, dsk.cx, dsk.cy,
                              "c:\\atui\\drive_desktop.bmp" );
        say( "done\n" );
        std::fclose( g_out );
        WinTerminate( g_hab );
        return 0;
    }

    if( argc > 1 && std::strcmp( argv[ 1 ], "--atui" ) == 0 )
    {
        driveAtui();
        say( "done\n" );
        std::fclose( g_out );
        WinTerminate( g_hab );
        return 0;
    }

    // ---- launch E by session spawn ------------------------------------
    {
        FILE *f = std::fopen( "c:\\os2\\e.exe", "rb" );
        if( f == 0 )
        {
            say( "no c:\\os2\\e.exe -- nothing to drive\n" );
            std::fclose( g_out );
            return 3;
        }
        std::fclose( f );

        TopList before;
        snapshotTops( before );

        STARTDATA sd;
        std::memset( &sd, 0, sizeof sd );
        sd.Length = (USHORT)sizeof sd;
        sd.Related = SSF_RELATED_INDEPENDENT;
        sd.FgBg = SSF_FGBG_BACK;       // the accepted form from here
        sd.SessionType = SSF_TYPE_PM;
        sd.PgmTitle = (PSZ)"ATUI DRIVE";
        sd.PgmName = (PSZ)"c:\\os2\\e.exe";
        ULONG agent = 0;
        PID pid = 0;
        APIRET rc = DosStartSession( &sd, &agent, &pid );
        say( "DosStartSession pm/background rc=%ld agent=%ld pid=%lu\n",
             (long)rc, (long)agent, (unsigned long)pid );
        if( rc != 0 )
        {
            std::fclose( g_out );
            return 4;
        }

        // ---- find it by what's new ------------------------------------
        HWND e = waitForNew( before, 8000 );
        if( e == 0 )
        {
            say( "find: no new window after launch\n" );
            std::fclose( g_out );
            return 5;
        }
        describeWindow( "E frame:", e );
        dumpChildren( e );

        // ---- title via WinSetWindowText (self-verifying) --------------
        BOOL swr = WinSetWindowText( e, "ATUI-DRIVE-OK" );
        CHAR title[ 64 ];
        title[ 0 ] = '\0';
        WinQueryWindowText( e, (LONG)sizeof title - 1, title );
        say( "WinSetWindowText %s, read back '%s'\n",
             yesno( swr ), title );

        // ---- keys to the editing child ---------------------------------
        const HWND edit = biggestChild( e );
        if( edit != 0 )
        {
            CHAR cls[ 48 ];
            cls[ 0 ] = '\0';
            WinQueryClassName( edit, (LONG)sizeof cls - 1, cls );
            say( "typing into h=%p class='%s' (sent WM_CHAR x15)\n",
                 (void *)edit, cls );
            const char *msg = "HELLO FROM ATUI";
            for( const char *p = msg; *p != '\0'; ++p )
                WinSendMsg( edit, WM_CHAR, MPFROMLONG( 0 ),
                            MPFROMLONG( (unsigned char)*p ) );
            pumpMs( 400 );
        }
        else
            say( "typing: no child to type into\n" );

        // ---- pixels as the proof of typing -----------------------------
        SWP swp;
        WinQueryWindowPos( e, &swp );
        if( captureScreenRectBmp( swp.x, swp.y, swp.cx, swp.cy,
                                  "c:\\atui\\drive_shot.bmp" ) )
            say( "pixels: drive_shot.bmp written -- fetch and look for "
                 "'HELLO FROM ATUI'\n" );

        // ---- ground truth that cannot move: the taskbar -----------------
        {
            HWND w = WinQueryWindow( HWND_DESKTOP, QW_TOP );
            HWND tb = 0;
            for( int i = 0; w != 0 && i < 96 && tb == 0; ++i )
            {
                CHAR cls[ 48 ];
                cls[ 0 ] = '\0';
                WinQueryClassName( w, (LONG)sizeof cls - 1, cls );
                if( std::strcmp( cls, "XWPCenterFrame" ) == 0 )
                    tb = w;
                w = WinQueryWindow( w, QW_NEXT );
            }
            if( tb != 0 )
            {
                SWP tsp;
                WinQueryWindowPos( tb, &tsp );
                say( "taskbar: h=%p x=%ld y=%ld %ldx%ld\n", (void *)tb,
                     (long)tsp.x, (long)tsp.y, (long)tsp.cx, (long)tsp.cy );
                if( captureScreenRectBmp( tsp.x, tsp.y, tsp.cx, tsp.cy,
                                          "c:\\atui\\drive_tb.bmp" ) )
                    say( "taskbar: drive_tb.bmp written -- one color "
                         "means capture is still broken\n" );
            }
            else
                say( "taskbar: XWPCenterFrame not found\n" );
        }

        // ---- close; E must ask about the unsaved changes ---------------
        TopList beforeClose;
        snapshotTops( beforeClose );
        say( "WM_CLOSE at the frame\n" );
        WinPostMsg( e, WM_CLOSE, MPVOID, MPVOID );
        pumpMs( 800 );

        HWND dlg = waitForDialog( beforeClose, 3000 );
        if( dlg != 0 )
        {
            describeWindow( "close-dialog:", dlg );
            dumpChildren( dlg );
            clickButtonNamed( dlg, "No" );
            pumpMs( 800 );
            say( "dialog after click: %s\n",
                 WinIsWindow( g_hab, dlg ) ? "STILL THERE" : "gone" );
        }
        else
            say( "close-dialog: none appeared (E closed straight away or "
                 "kept its window)\n" );

        DosSleep( 800 );
        say( "E frame after close: %s\n",
             WinIsWindow( g_hab, e ) ? "STILL THERE" : "gone" );
    }

    say( "done\n" );
    std::fclose( g_out );
    WinTerminate( g_hab );
    return 0;
}
