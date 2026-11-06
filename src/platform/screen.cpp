// The parts of the desktop seam that hold no desktop: the text a window
// listing becomes, the way a query finds a window, and where a shot goes
// when nobody said. See screen.h.
#include "platform/screen.h"

#include "tui/ascii_fold.h"

#include <cstdio>
#include <cstring>

namespace atui {
namespace platform {

namespace {

// ASCII-lower, for matching a title the way a person types one.
std::string lowerAscii( const std::string &s )
{
    std::string out( s );
    for( size_t i = 0; i < out.size(); ++i )
    {
        if( out[ i ] >= 'A' && out[ i ] <= 'Z' )
            out[ i ] = (char)( out[ i ] - 'A' + 'a' );
    }
    return out;
}

bool endsWithBmp( const std::string &s )
{
    if( s.size() < 4 )
        return false;
    std::string tail = lowerAscii( s.substr( s.size() - 4 ) );
    return tail == ".bmp" || tail == ".png" || tail == ".jpg";
}

// The quote character, backslash-escaped: a title in the listing should
// survive being read back by a model as exactly one window name.
std::string quoted( const std::string &s )
{
    std::string out( "\"" );
    for( size_t i = 0; i < s.size(); ++i )
    {
        if( s[ i ] == '"' )
            out += "\\\"";
        else if( s[ i ] == '\n' || s[ i ] == '\r' )
            out += ' ';
        else
            out += s[ i ];
    }
    out += '"';
    return out;
}

void formatOne( const WindowInfo &win, const char *indent,
                std::string &out )
{
    char geom[ 64 ];
    std::snprintf( geom, sizeof geom, "%ix%i %ix%i",
                   win.x, win.y, win.w, win.h );

    out += indent;
    out += win.handle;
    out += ' ';
    out += geom;
    out += ' ';
    out += win.title.empty() ? "\"\"" : quoted( atui::tui::foldToAscii( win.title ) );
    out += "  class:";
    out += win.cls.empty() ? "-" : atui::tui::foldToAscii( win.cls );
    if( win.active )
        out += "  ACTIVE";
    if( win.iconic )
        out += "  iconic";
    out += '\n';
}

} // anonymous namespace

std::string formatWindows( const std::vector<WindowInfo> &list )
{
    std::string out;
    for( size_t i = 0; i < list.size(); ++i )
    {
        formatOne( list[ i ], "", out );
        for( size_t c = 0; c < list[ i ].children.size(); ++c )
            formatOne( list[ i ].children[ c ], "  ", out );
    }
    return out;
}

void findWindows( const std::vector<WindowInfo> &list,
                  const std::string &query,
                  std::vector< const WindowInfo * > &out )
{
    out.clear();
    if( query.empty() )
        return;

    // Handle first, exact (either spelling: 0x... and bare digits).
    for( size_t i = 0; i < list.size(); ++i )
    {
        if( list[ i ].handle == query )
        {
            out.push_back( &list[ i ] );
            return;
        }
    }

    const std::string q = lowerAscii( query );
    for( size_t i = 0; i < list.size(); ++i )
    {
        if( !list[ i ].title.empty()
            && lowerAscii( list[ i ].title ).find( q ) != std::string::npos )
            out.push_back( &list[ i ] );
    }
    if( !out.empty() )
        return;

    for( size_t i = 0; i < list.size(); ++i )
    {
        if( !list[ i ].cls.empty()
            && lowerAscii( list[ i ].cls ).find( q ) != std::string::npos )
            out.push_back( &list[ i ] );
    }
}

std::string defaultShotPath()
{
    for( int n = 1; n <= 999; ++n )
    {
        char name[ 32 ];
        std::snprintf( name, sizeof name, "atui_shot_%d.bmp", n );
        if( std::FILE *probe = std::fopen( name, "rb" ) )
        {
            std::fclose( probe );
            continue;   // taken; try the next number
        }
        return std::string( name );
    }
    return std::string();  // nine hundred and ninety-nine shots deep: stop
}

// Give a requested path the extension it needs to be the file it will be.
std::string ensureImagePath( const std::string &path )
{
    if( path.empty() || endsWithBmp( path ) )
        return path;
    return path + ".bmp";
}

} // namespace atui::platform
} // namespace atui
