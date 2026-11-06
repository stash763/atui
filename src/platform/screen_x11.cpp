// The desktop seam over X11: capture with XGetImage, enumerate with
// XQueryTree and the ICCCM/EWMH properties every window manager answers.
//
// Built when the configure found X11 (ATUI_HAVE_X11); links nothing beyond
// libX11 itself.  FLTK, when the unix GUI front arrives, will want the
// same library for its own reasons, so this adds no dependency to that
// future -- and it stays useful without FLTK, which is the point of asking
// the X server directly instead of through a toolkit.
//
// Two decisions the X11 world argues about, taken here on evidence:
//
//   * Window capture reads the window's own pixels (XGetImage on the
//     window), not its rectangle off the root.  Measured against XQuartz:
//     a compositor that keeps the screen in its own storage answers a
//     root grab with black and a window grab with the real contents.  The
//     mirror-image risk -- stale regions on an ancient unredirected
//     server -- loses to actual window contents every time.
//   * Xlib exits the process on an unhandled protocol error.  A window
//     that dies between the listing and the grab is a normal race in a
//     desktop session and must not take the chat with it, so an error
//     handler that swallows and reports is installed for the whole
//     process; every call below still checks its own result.
#include "platform/screen.h"
#include "platform/bmpout.h"

#if defined( ATUI_HAVE_X11 )

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace atui {
namespace platform {

namespace {

int xerrorSilently( Display *, XErrorEvent * )
{
    return 0;   // a window died mid-call: the return values say so
}

void ensureErrorHandler()
{
    static bool installed = false;
    if( !installed )
    {
        XSetErrorHandler( xerrorSilently );
        installed = true;
    }
}

std::string handleOf( Window w )
{
    char buf[ 32 ];
    std::snprintf( buf, sizeof buf, "0x%lx", (unsigned long)w );
    return std::string( buf );
}

// Opens the display or reports why not in `err`.
Display *openDisplay( std::string &err )
{
    ensureErrorHandler();
    Display *dpy = XOpenDisplay( 0 );
    if( dpy == 0 )
        err = "cannot open the X display (is DISPLAY set and a session "
              "running?); under Wayland without XWayland this tool has "
              "no purchase";
    return dpy;
}

// One NUL-terminated text property, as a string.
bool textProperty( Display *dpy, Window w, Atom type, std::string &out )
{
    Atom actualType; int actualFormat;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char *data = 0;
    if( XGetWindowProperty( dpy, w, type, 0, 4096, False, AnyPropertyType,
                            &actualType, &actualFormat, &nItems,
                            &bytesAfter, &data ) != Success )
        return false;
    bool found = false;
    if( data != 0 && actualFormat == 8 && nItems > 0 )
    {
        out.assign( (const char *)data, (size_t)nItems );
        // WM_NAME/UTF8 strings come NUL-terminated; keep the first
        // NUL-free run.
        size_t nul = out.find( '\0' );
        if( nul != std::string::npos )
            out.erase( nul );
        found = !out.empty();
    }
    if( data != 0 )
        XFree( data );
    return found;
}

// WM_CLASS is "instance\0class\0": prefer the class (second string).
bool windowClass( Display *dpy, Window w, std::string &out )
{
    Atom actualType; int actualFormat;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char *data = 0;
    if( XGetWindowProperty( dpy, w, XA_WM_CLASS, 0, 256, False, XA_STRING,
                            &actualType, &actualFormat, &nItems,
                            &bytesAfter, &data ) != Success || data == 0 )
        return false;
    bool found = false;
    if( actualFormat == 8 && nItems > 0 )
    {
        const char *s = (const char *)data;
        const char *second = s;
        size_t used = 0;
        if( *second != '\0' )   // skip instance
            used += std::strlen( second ) + 1;
        second = s + used;
        if( used < nItems && *second != '\0' )
        {
            out = second;
            found = true;
        }
        else if( used > 1 )
        {
            out = s;            // only one string present: use it
            found = true;
        }
    }
    XFree( data );
    return found;
}

// _NET_ACTIVE_WINDOW on the root, if the WM answers.
Window activeWindow( Display *dpy )
{
    Atom netActive = XInternAtom( dpy, "_NET_ACTIVE_WINDOW", False );
    if( netActive == None )
        return 0;   // no EWMH WM: nobody is the active window to name
    Atom actualType; int actualFormat;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char *data = 0;
    Window active = 0;
    if( XGetWindowProperty( dpy, DefaultRootWindow( dpy ), netActive,
                            0, 1, False, XA_WINDOW,
                            &actualType, &actualFormat, &nItems,
                            &bytesAfter, &data ) == Success
        && data != 0 && actualFormat == 32 && nItems == 1 )
        active = (Window)*(unsigned long *)data;
    if( data != 0 )
        XFree( data );
    return active;
}

bool windowStateIconic( Display *dpy, Window w )
{
    // WM_STATE is not a predefined atom: the ICCCM makes it the WM's to
    // intern, so asking without create is exact -- if the atom has never
    // been interned, no window has a state to read.
    static Atom wmState = None;
    static bool interned = false;
    if( !interned )
    {
        wmState  = XInternAtom( dpy, "WM_STATE", False );
        interned = true;
    }
    if( wmState == None )
        return false;

    Atom actualType; int actualFormat;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char *data = 0;
    bool iconic = false;
    if( XGetWindowProperty( dpy, w, wmState, 0, 2, False,
                            wmState, &actualType, &actualFormat,
                            &nItems, &bytesAfter, &data ) == Success
        && data != 0 && actualFormat == 32 && nItems >= 1 )
        iconic = (int)*(unsigned long *)data == IconicState;
    if( data != 0 )
        XFree( data );
    return iconic;
}

// Title: EWMH UTF-8 first, WM_NAME second -- the way a modern WM expects.
void windowTitle( Display *dpy, Window w, std::string &out )
{
    static Atom netName = None;
    static Atom utf8 = None;
    static bool interned = false;
    if( !interned )
    {
        netName = XInternAtom( dpy, "_NET_WM_NAME", False );
        utf8    = XInternAtom( dpy, "UTF8_STRING", False );
        interned = true;
    }
    Atom actualType; int actualFormat;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char *data = 0;
    if( netName != None
        && XGetWindowProperty( dpy, w, netName, 0, 2048, False, utf8,
                               &actualType, &actualFormat, &nItems,
                               &bytesAfter, &data ) == Success
        && data != 0 && nItems > 0 )
    {
        out.assign( (const char *)data, (size_t)nItems );
        size_t nul = out.find( '\0' );
        if( nul != std::string::npos )
            out.erase( nul );
    }
    if( data != 0 )
        XFree( data );
    if( out.empty() )
        textProperty( dpy, w, XA_WM_NAME, out );
}

bool fillWindow( Display *dpy, Window w, Window active, WindowInfo &info )
{
    XWindowAttributes attrs;
    if( !XGetWindowAttributes( dpy, w, &attrs ) )
        return false;
    if( attrs.width <= 0 || attrs.height <= 0 )
        return false;

    const bool iconic = windowStateIconic( dpy, w );
    if( attrs.map_state != IsViewable && !iconic )
        return false;

    info.handle = handleOf( w );
    info.w = attrs.width;
    info.h = attrs.height;
    if( !iconic )
    {
        Window child = 0;
        // attrs.x/y are parent-relative; frames belong to the WM, and the
        // screen rectangle is what both the listing and the grab want.
        XTranslateCoordinates( dpy, w, DefaultRootWindow( dpy ),
                               0, 0, &info.x, &info.y, &child );
    }
    info.active = ( w == active );
    info.iconic = iconic;
    windowTitle( dpy, w, info.title );
    windowClass( dpy, w, info.cls );
    return true;
}

// Channel geometry: an X image hands over three masks; extract, scale to
// 8 bits (replicating the top bits the server dropped, the way every
// scaler worth its colors does), done.
int maskShift( unsigned long mask )
{
    int s = 0;
    if( mask == 0 )
        return -1;
    while( ( mask & 1UL ) == 0 )
    {
        mask >>= 1;
        ++s;
    }
    return s;
}

int maskBits( unsigned long mask )
{
    int b = 0;
    while( ( mask & 1UL ) != 0 )
    {
        mask >>= 1;
        ++b;
    }
    return b;
}

unsigned char scaleChannel( unsigned long v, int bits )
{
    if( bits <= 0 )
        return 0;
    if( bits >= 8 )
        return (unsigned char)( ( v >> ( bits - 8 ) ) & 0xFF );
    unsigned long out = v << ( 8 - bits );
    return (unsigned char)( ( out | ( out >> bits ) ) & 0xFF );
}

// Convert a grabbed truecolor ZPixmap and write the BMP. The X server
// answers in whatever visual the screen has; the masks ride on the image.
CaptureResult convertAndWrite( XImage *img, int w, int h,
                               const std::string &path )
{
    CaptureResult res;

    const int depth = img->depth;
    if( depth != 16 && depth != 24 && depth != 32 )
    {
        char dbuf[ 16 ];
        std::snprintf( dbuf, sizeof dbuf, "%d", depth );
        res.error = "unsupported screen depth (pixels are "
                    + std::string( dbuf ) + " bits here)";
        XDestroyImage( img );
        return res;
    }

    const unsigned long rmask = (unsigned long)img->red_mask;
    const unsigned long gmask = (unsigned long)img->green_mask;
    const unsigned long bmask = (unsigned long)img->blue_mask;
    const int rShift = maskShift( rmask );
    const int gShift = maskShift( gmask );
    const int bShift = maskShift( bmask );
    if( rShift < 0 || gShift < 0 || bShift < 0 )
    {
        res.error = "screen has no red/green/blue to extract (is this "
                    "pseudocolor?)";
        XDestroyImage( img );
        return res;
    }
    const int rBits  = maskBits( rmask >> rShift );
    const int gBits  = maskBits( gmask >> gShift );
    const int bBits  = maskBits( bmask >> bShift );

    std::vector<unsigned char> rgb( (size_t)w * (size_t)h * 3 );
    for( int yy = 0; yy < h; ++yy )
    {
        for( int xx = 0; xx < w; ++xx )
        {
            const unsigned long px = XGetPixel( img, xx, yy );
            unsigned char *out = &rgb[ ( (size_t)yy * (size_t)w + xx ) * 3 ];
            out[ 0 ] = scaleChannel( ( px >> rShift )
                                     & ( ( 1UL << rBits ) - 1 ), rBits );
            out[ 1 ] = scaleChannel( ( px >> gShift )
                                     & ( ( 1UL << gBits ) - 1 ), gBits );
            out[ 2 ] = scaleChannel( ( px >> bShift )
                                     & ( ( 1UL << bBits ) - 1 ), bBits );
        }
    }
    XDestroyImage( img );

    std::string err;
    if( !writeBmp24( path, &rgb[ 0 ], w, h, err ) )
    {
        res.error = err;
        return res;
    }

    res.ok = true;
    res.path = path;
    res.width = w;
    res.height = h;
    return res;
}

// Grab a rectangle of the root window -- the whole screen, in practice.
// (Under XQuartz the root never holds pixels -- macOS's WindowServer does
// the compositing there -- but XQuartz is not this backend's target; a
// Mac build gets screen_mac.cpp. Linux and BSD servers, composited or
// not, answer with what was drawn.)
CaptureResult grabRootRect( Display *dpy, int x, int y, int w, int h,
                            const std::string &path )
{
    CaptureResult res;
    if( x < 0 ) x = 0;
    if( y < 0 ) y = 0;
    const int screenW = WidthOfScreen( DefaultScreenOfDisplay( dpy ) );
    const int screenH = HeightOfScreen( DefaultScreenOfDisplay( dpy ) );
    if( x + w > screenW ) w = screenW - x;
    if( y + h > screenH ) h = screenH - y;
    if( w <= 0 || h <= 0 )
    {
        res.error = "the window is off the screen";
        return res;
    }

    XImage *img = XGetImage( dpy, DefaultRootWindow( dpy ), x, y,
                             (unsigned)w, (unsigned)h,
                             AllPlanes, ZPixmap );
    if( img == 0 )
    {
        res.error = "XGetImage failed (nothing to see here, or the "
                    "server said no)";
        return res;
    }
    return convertAndWrite( img, w, h, path );
}

unsigned long parseHandle( const std::string &handle )
{
    const char *s = handle.c_str();
    if( s[ 0 ] == '0' && ( s[ 1 ] == 'x' || s[ 1 ] == 'X' ) )
        s += 2;
    char *end = 0;
    const unsigned long v = std::strtoul( s, &end, 16 );
    if( end == s || ( *end != '\0' ) )
        return 0;
    return v;
}

} // anonymous namespace

bool captureAvailable()
{
    std::string err;
    Display *dpy = openDisplay( err );
    if( dpy == 0 )
        return false;
    XCloseDisplay( dpy );
    return true;
}

bool windowsAvailable()
{
    return captureAvailable();
}

CaptureResult captureScreen( const std::string &path )
{
    CaptureResult res;
    std::string err;
    Display *dpy = openDisplay( err );
    if( dpy == 0 )
    {
        res.error = err;
        return res;
    }
    res = grabRootRect( dpy, 0, 0,
                        WidthOfScreen( DefaultScreenOfDisplay( dpy ) ),
                        HeightOfScreen( DefaultScreenOfDisplay( dpy ) ),
                        path );
    XCloseDisplay( dpy );
    return res;
}

CaptureResult captureWindow( const std::string &handle,
                             const std::string &path )
{
    CaptureResult res;
    std::string err;
    Display *dpy = openDisplay( err );
    if( dpy == 0 )
    {
        res.error = err;
        return res;
    }

    const unsigned long id = parseHandle( handle );
    if( id == 0 )
    {
        res.error = "not a window handle (use one of the handles from "
                    "the windows tool)";
        XCloseDisplay( dpy );
        return res;
    }

    XWindowAttributes attrs;
    if( !XGetWindowAttributes( dpy, (Window)id, &attrs )
        || attrs.width <= 0 || attrs.height <= 0 )
    {
        res.error = "no window " + handle + " (closed, or never was)";
        XCloseDisplay( dpy );
        return res;
    }
    if( windowStateIconic( dpy, (Window)id ) )
    {
        res.error = "window " + handle + " is minimized -- its contents "
                    "are given up while iconic";
        XCloseDisplay( dpy );
        return res;
    }

    // The window's own pixels, measured here against XQuartz's
    // compositor: reading the root at the window's rectangle answers
    // black on servers whose compositor does not draw into the root
    // framebuffer, while XGetImage on the window answers with the real
    // contents.  The cost is the mirror-image case: on an old
    // unredirected server, regions no server has remembered since they
    // were last exposed may come out stale.  Real window contents beat
    // a guarantee about stale pixels.
    XImage *img = XGetImage( dpy, (Window)id, 0, 0,
                             (unsigned)attrs.width, (unsigned)attrs.height,
                             AllPlanes, ZPixmap );
    if( img == 0 )
    {
        res.error = "XGetImage on window " + handle
                  + " failed (it may have just closed)";
        XCloseDisplay( dpy );
        return res;
    }
    res = convertAndWrite( img, attrs.width, attrs.height, path );
    XCloseDisplay( dpy );
    return res;
}

bool listWindows( std::vector<WindowInfo> &out, bool children )
{
    out.clear();
    std::string err;
    Display *dpy = openDisplay( err );
    if( dpy == 0 )
        return false;

    Window root = 0, immediate = 0;
    Window *wins = 0;
    unsigned int n = 0;
    const Status ok = XQueryTree( dpy, DefaultRootWindow( dpy ),
                                  &root, &immediate, &wins, &n );
    if( ok == 0 )
    {
        XCloseDisplay( dpy );
        return false;
    }

    const Window active = activeWindow( dpy );
    for( unsigned int i = 0; i < n; ++i )
    {
        WindowInfo info;
        if( !fillWindow( dpy, wins[ i ], active, info ) )
            continue;
        if( children )
        {
            Window rootRet = 0, parentRet = 0;
            Window *kids = 0;
            unsigned int kn = 0;
            if( XQueryTree( dpy, wins[ i ], &rootRet, &parentRet,
                            &kids, &kn ) )
            {
                for( unsigned int k = 0; k < kn; ++k )
                {
                    WindowInfo kid;
                    if( fillWindow( dpy, kids[ k ], active, kid ) )
                        info.children.push_back( kid );
                }
                if( kids != 0 )
                    XFree( kids );
            }
        }
        out.push_back( info );
    }
    if( wins != 0 )
        XFree( wins );
    XCloseDisplay( dpy );
    return true;
}

} // namespace atui::platform
} // namespace atui

#endif // ATUI_HAVE_X11
