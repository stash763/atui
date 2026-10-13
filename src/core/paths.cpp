// Implementation of the install-location and filesystem helpers. See paths.h.
#include "paths.h"

#include <cstdlib>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#define ATUI_MKDIR( p )      _mkdir( p )
#define ATUI_ACCESS( p, m )  _access( ( p ), ( m ) )
#else
#include <sys/stat.h>
#include <unistd.h>
#define ATUI_MKDIR( p )      ::mkdir( ( p ), 0700 )
#define ATUI_ACCESS( p, m )  ::access( ( p ), ( m ) )
#endif

namespace atui {
namespace core {

namespace {

// Both separators, because DOS/Windows accept either and argv[0] arrives with
// whichever the launcher used.
std::string::size_type lastSep( const std::string &s )
{
    const std::string::size_type a = s.find_last_of( '/' );
    const std::string::size_type b = s.find_last_of( '\\' );
    if( a == std::string::npos ) return b;
    if( b == std::string::npos ) return a;
    return ( a > b ) ? a : b;
}

std::string envOrNone( const char *name )
{
    const char *v = std::getenv( name );
    return ( v && *v ) ? std::string( v ) : std::string();
}

std::string &startupArgv0()
{
    static std::string a;
    return a;
}

} // anonymous namespace

void setArgv0( const char *arg )
{
    if( arg ) startupArgv0() = std::string( arg );
}

std::string installDir()
{
    std::string home = envOrNone( "ATUI_HOME" );
    if( !home.empty() ) return home;

    const std::string &a = startupArgv0();
    if( !a.empty() )
    {
        const std::string::size_type sep = lastSep( a );
        if( sep != std::string::npos )
        {
            // "C:\atui\atui.exe" -> "C:\atui", and "/" for a bare "/atui".
            std::string dir = a.substr( 0, sep );
            if( dir.empty() ) dir = "/";
            return dir;
        }
        // A bare "atui" means it came from PATH: the compiled-in location is the
        // only remaining guess, and it is the one the packager knew.
    }

#ifdef ATUI_INSTALL_DIR
    return std::string( ATUI_INSTALL_DIR );
#else
    // Nothing to go on. The current directory is not where atui was installed,
    // but it is writable and it is where the user is looking.
    return std::string( "." );
#endif
}

bool isDirectory( const std::string &path )
{
#if defined(_WIN32)
    struct _stat st;
    if( ::_stat( path.c_str(), &st ) != 0 ) return false;
    return ( st.st_mode & _S_IFDIR ) != 0;
#else
    struct stat st;
    if( ::stat( path.c_str(), &st ) != 0 ) return false;
    return S_ISDIR( st.st_mode ) != 0;
#endif
}

bool makeDirectory( const std::string &path )
{
    return ATUI_MKDIR( path.c_str() ) == 0;
}

bool isWritableDir( const std::string &path )
{
    if( path.empty() ) return false;
    // W_OK (2). On the DOS-derived filesystems this is a coarse question - FAT
    // has no write permission at all, so it reports writable and the actual
    // create fails later, which `begin` handles - but it catches the case worth
    // catching here: a read-only install tree or a full disk mount.
    return ATUI_ACCESS( path.c_str(), 2 ) == 0;
}

bool isWritableFile( const std::string &path )
{
    if( path.empty() ) return false;
    if( fileSize( path ) < 0 ) return false;    // not there, so nothing to write
    return ATUI_ACCESS( path.c_str(), 2 ) == 0;
}

bool setWorkingDirectory( const std::string &path )
{
    if( path.empty() ) return false;
#if defined(_WIN32)
    return ::_chdir( path.c_str() ) == 0;
#else
    return ::chdir( path.c_str() ) == 0;
#endif
}

std::string absolutePath( const std::string &path )
{
    if( path.empty() ) return path;
    if( path[0] == '/' || path[0] == '\\' ) return path;
#if defined(_WIN32)
    if( path.size() > 1 && path[1] == ':' ) return path;   // "C:\dir\file"
#endif
    char buf[ 512 ];
#if defined(_WIN32)
    if( !_getcwd( buf, sizeof buf ) ) return path;
#else
    if( !::getcwd( buf, sizeof buf ) ) return path;
#endif
    std::string cwd( buf );
    if( cwd.empty() ) return path;
    const char last = cwd[cwd.size() - 1];
    if( last != '/' && last != '\\' ) cwd += "/";
    return cwd + path;
}

std::string joinDir( const std::string &dir, const std::string &leaf )
{
    if( dir.empty() ) return leaf;
    const char last = dir[dir.size() - 1];
    if( last == '/' || last == '\\' ) return dir + leaf;
    // Forward slashes are accepted by every DOS-derived API at this level, and
    // writing them means no caller has to know which platform it is on.
    return dir + "/" + leaf;
}

long fileSize( const std::string &path )
{
    // Seek rather than stat: the size of a file is the only fact needed, and an
    // open/seek/close cycle behaves identically on every toolchain atui builds
    // for. Directories fail to seek, which reports as -1 - the same answer as
    // "not there", and correct for every caller, which wants a regular file.
    std::ifstream f( path.c_str(), std::ios::in | std::ios::binary | std::ios::ate );
    if( !f ) return -1;
    const std::streampos end = f.tellg();
    if( end < 0 ) return -1;
    return (long) end;
}

} // namespace atui::core
} // namespace atui
