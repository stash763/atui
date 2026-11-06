// Tests for the desktop seam's testable half: the BMP writer and reader,
// the window query, and the listing a model reads. None of these needs a
// desktop -- which is the point of keeping them separate from the
// backends, and why the X11 and macOS code can be wrong on the host and
// still not damage these guarantees.
#include "harness.h"
#include "platform/bmpout.h"
#include "platform/screen.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace atui_test {

namespace {

using atui::platform::WindowInfo;

const char *kTmpBmp = "atui_screen_test.bmp";

WindowInfo makeWindow( const char *handle, const char *title,
                       const char *cls )
{
    WindowInfo w;
    w.handle = handle;
    w.title = title;
    w.cls = cls;
    w.x = 10; w.y = 20; w.w = 100; w.h = 50;
    return w;
}

// --- BMP writer ---------------------------------------------------------

int test_bmp24_roundtrip()
{
    // Two pixels of known color, top-down RGB.
    const unsigned char px[ 12 ] = {
        0x10, 0x20, 0x30,     // top row, left
        0x40, 0x50, 0x60,     // top row, right
        0x70, 0x80, 0x90,     // bottom row, left
        0xA0, 0xB0, 0xC0      // bottom row, right
    };
    std::string err;
    std::remove( kTmpBmp );
    if( !atui::platform::writeBmp24( kTmpBmp, px, 2, 2, err ) )
        return -1;

    int w = 0, h = 0, bpp = 0;
    if( !atui::platform::bmpHeaderInfo( kTmpBmp, w, h, bpp, err ) )
        return -2;
    if( w != 2 || h != 2 )
        return -3;

    // Geometry of the file: 54-byte header, rows padded to 4 bytes
    // (2 px * 3 B -> stride 8), so 54 + 16 = 70 bytes total.
    std::FILE *f = std::fopen( kTmpBmp, "rb" );
    if( f == 0 )
        return -4;
    unsigned char all[ 128 ];
    const size_t n = std::fread( all, 1, sizeof all, f );
    std::fclose( f );
    if( n != 70 )
        return -5;
    if( all[ 2 ] != 70 )
        return -6;
    if( all[ 10 ] != 54 )
        return -7;

    // BMP rows run bottom-up and bytes run BGR: the BOTTOM row is stored
    // first -- pixel (0,1) blue-first, then its red neighbor.
    if( all[ 54 ] != 0x90 || all[ 55 ] != 0x80 || all[ 56 ] != 0x70 )
        return -8;
    // Row stride pads, so the top stored row starts at 54 + 8.
    if( all[ 62 ] != 0x30 || all[ 63 ] != 0x20 || all[ 64 ] != 0x10 )
        return -9;

    std::remove( kTmpBmp );
    return 0;
}

int test_bmp_reader_rejects_rubbish()
{
    std::FILE *f = std::fopen( kTmpBmp, "wb" );
    if( f == 0 )
        return -1;
    std::fwrite( "not a bitmap at all, not nearly", 1, 28, f );
    std::fclose( f );

    int w = 0, h = 0, bpp = 0;
    std::string err;
    const bool ok = atui::platform::bmpHeaderInfo( kTmpBmp, w, h, bpp, err );
    std::remove( kTmpBmp );
    return ok ? -2 : 0;
}

int test_bmp_reader_os2_core_header()
{
    // The 12-byte BITMAPCOREHEADER variant, and top-down (negative
    // height): what macOS's screencapture -t bmp writes.
    unsigned char f[ 26 ] = {
        'B', 'M', 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0,          // offsets 8-13 unused
        12, 0, 0, 0,               // DIB size 12
        4, 0,                      // width 4
        0xFD, 0xFF,                // height -3 (signed 16-bit, top-down)
        1, 0,                      // planes
        24, 0                      // bpp
    };
    std::remove( kTmpBmp );
    std::FILE *out = std::fopen( kTmpBmp, "wb" );
    if( out == 0 )
        return -1;
    if( std::fwrite( f, 1, sizeof f, out ) != sizeof f )
    {
        std::fclose( out );
        return -2;
    }
    std::fclose( out );

    int w = 0, h = 0, bpp = 0;
    std::string err;
    if( !atui::platform::bmpHeaderInfo( kTmpBmp, w, h, bpp, err ) )
        return -3;
    std::remove( kTmpBmp );
    if( w != 4 || h != 3 || bpp != 24 )
        return -4;
    return 0;
}

// --- window query -------------------------------------------------------

int test_find_handle_exact()
{
    std::vector<WindowInfo> all;
    all.push_back( makeWindow( "0x1a00003", "First", "XTerm" ) );
    all.push_back( makeWindow( "0x2b00007", "Second", "XTerm" ) );

    std::vector<const WindowInfo *> hits;
    atui::platform::findWindows( all, "0x2b00007", hits );
    if( hits.size() != 1 || hits[ 0 ]->title != "Second" )
        return -1;
    return 0;
}

int test_find_title_substring_case_insensitive()
{
    std::vector<WindowInfo> all;
    all.push_back( makeWindow( "0x1", "gedit — text editor", "Gedit" ) );
    all.push_back( makeWindow( "0x2", "Terminal", "XTerm" ) );

    std::vector<const WindowInfo *> hits;
    atui::platform::findWindows( all, "TEXT", hits );
    if( hits.size() != 1 || hits[ 0 ]->handle != "0x1" )
        return -1;

    atui::platform::findWindows( all, "terminal", hits );
    if( hits.size() != 1 || hits[ 0 ]->handle != "0x2" )
        return -2;
    return 0;
}

int test_find_class_only_when_title_misses()
{
    std::vector<WindowInfo> all;
    all.push_back( makeWindow( "0x1", "untitled", "Gedit" ) );
    all.push_back( makeWindow( "0x2", "other", "Gedit" ) );

    std::vector<const WindowInfo *> hits;
    atui::platform::findWindows( all, "gedit", hits );
    if( hits.size() != 2 )
        return -1;

    atui::platform::findWindows( all, "no such thing", hits );
    if( !hits.empty() )
        return -2;

    atui::platform::findWindows( all, "", hits );
    if( !hits.empty() )
        return -3;   // an empty query matches nothing; callers list
    return 0;
}

// --- listing text -------------------------------------------------------

int test_format_windows_shape()
{
    std::vector<WindowInfo> all;
    WindowInfo top = makeWindow( "0x1a", "Chat", "Atui" );
    top.active = true;
    top.children.push_back( makeWindow( "0x1b", "child", "Entry" ) );
    WindowInfo min = makeWindow( "0x2c", "", "" );
    min.iconic = true;
    all.push_back( top );
    all.push_back( min );

    const std::string out = atui::platform::formatWindows( all );
    if( out.find( "0x1a 10x20 100x50 \"Chat\"  class:Atui  ACTIVE" )
        == std::string::npos )
        return -1;
    if( out.find( "  0x1b 10x20 100x50 \"child\"  class:Entry" )
        == std::string::npos )
        return -2;   // children indented
    if( out.find( "class:-" ) == std::string::npos )
        return -3;   // empty title/class still shape
    if( out.find( "iconic" ) == std::string::npos )
        return -4;
    return 0;
}

int test_format_folds_titles()
{
    std::vector<WindowInfo> all;
    // An em dash in a title must not survive to a CP850 screen intact as
    // bytes -- it becomes '-' (see tui/ascii_fold.h).
    all.push_back( makeWindow( "0x1", "a\xe2\x80\x94""b", "X" ) );
    const std::string out = atui::platform::formatWindows( all );
    if( out.find( "\"a-b\"" ) == std::string::npos )
        return -1;
    return 0;
}

// --- path helpers ---------------------------------------------------------

int test_ensure_image_path()
{
    if( !atui::platform::ensureImagePath( "" ).empty() )
        return -1;
    if( atui::platform::ensureImagePath( "result" ) != "result.bmp" )
        return -2;
    if( atui::platform::ensureImagePath( "x.bmp" ) != "x.bmp" )
        return -3;
    if( atui::platform::ensureImagePath( "x.PNG" ) != "x.PNG" )
        return -4;
    return 0;
}

int test_default_shot_path_shape()
{
    // Probing must not create anything; it just has to hand back a free
    // name of the right shape.
    const std::string p = atui::platform::defaultShotPath();
    if( p.substr( 0, 10 ) != "atui_shot_" )
        return -1;
    if( p.size() < 15 || p.substr( p.size() - 4 ) != ".bmp" )
        return -2;
    return 0;
}

} // anonymous namespace

void init_screen_tests()
{
    register_test( "bmp24_roundtrip", test_bmp24_roundtrip );
    register_test( "bmp_reader_rejects_rubbish",
                   test_bmp_reader_rejects_rubbish );
    register_test( "bmp_reader_os2_core_header",
                   test_bmp_reader_os2_core_header );
    register_test( "find_handle_exact", test_find_handle_exact );
    register_test( "find_title_substring",
                   test_find_title_substring_case_insensitive );
    register_test( "find_class_fallback",
                   test_find_class_only_when_title_misses );
    register_test( "format_windows_shape", test_format_windows_shape );
    register_test( "format_folds_titles", test_format_folds_titles );
    register_test( "ensure_image_path", test_ensure_image_path );
    register_test( "default_shot_path_shape", test_default_shot_path_shape );
}

} // namespace atui_test
