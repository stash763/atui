// The desktop seam on macOS, through Apple's own capture command.
//
// /usr/sbin/screencapture is the supported way to grab pixels on this
// platform short of linking CoreGraphics through Objective-C, and a
// development host's screenshot tool is exactly the case it was written
// for.  `-x` keeps it silent (a shutter sound during an automated test
// loop is a bug), `-t bmp` keeps the file one this repo can read back and
// verify -- macOS writes the old 12-byte-header BMP variant, which
// bmpout.cpp's reader was made for.
//
// Window listing is not offered here: enumerating windows with handles
// needs CGWindowListCopyWindowInfo, which is fine but is Objective-C and
// its own phase.  Without the listing there are no handles to capture, so
// window capture declines too -- and the windows tool is not registered.
//
// One honesty note the code cannot fix: since Catalina, the first grab
// from a terminal needs the Screen Recording permission, and until it is
// granted screencapture succeeds quietly with a black picture.  Users of
// this backend grant that once.
#include "platform/screen.h"
#include "platform/bmpout.h"

#if defined( ATUI_HAVE_MAC_CAPTURE )

#include <cstdio>
#include <string>

namespace atui {
namespace platform {

namespace {

const char *kCaptureBin = "/usr/sbin/screencapture";

// Single-quote for /bin/sh, with the '\'' dance for embedded quotes.
std::string shellQuote( const std::string &s )
{
    std::string out( "'" );
    for( size_t i = 0; i < s.size(); ++i )
    {
        if( s[ i ] == '\'' )
            out += "'\\''";
        else
            out += s[ i ];
    }
    out += '\'';
    return out;
}

} // anonymous namespace

bool captureAvailable()
{
    if( std::FILE *probe = std::fopen( kCaptureBin, "rb" ) )
    {
        std::fclose( probe );
        return true;
    }
    return false;
}

bool windowsAvailable()
{
    return false;   // see the header comment: no enumeration yet
}

CaptureResult captureScreen( const std::string &path )
{
    CaptureResult res;
    if( !captureAvailable() )
    {
        res.error = std::string( kCaptureBin ) + " not found";
        return res;
    }

    const std::string cmd = std::string( kCaptureBin )
                          + " -x -t bmp " + shellQuote( path ) + " 2>/dev/null";
    if( std::system( cmd.c_str() ) != 0 )
    {
        res.error = "screencapture failed (screen recording permission? "
                    "check System Settings > Privacy)";
        return res;
    }

    int w = 0, h = 0, bpp = 0;
    std::string err;
    if( !bmpHeaderInfo( path, w, h, bpp, err ) )
    {
        res.error = "screencapture wrote no readable BMP: " + err;
        return res;
    }

    res.ok = true;
    res.path = path;
    res.width = w;
    res.height = h;
    return res;
}

CaptureResult captureWindow( const std::string &, const std::string & )
{
    CaptureResult res;
    res.error = "window capture is not available on this platform yet; "
                "capture the whole screen instead";
    return res;
}

bool listWindows( std::vector<WindowInfo> &, bool )
{
    return false;
}

} // namespace atui::platform
} // namespace atui

#endif // ATUI_HAVE_MAC_CAPTURE
