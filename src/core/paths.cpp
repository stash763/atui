// Implementation of the install-location and filesystem helpers. See paths.h.
#include "paths.h"

#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

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

const char kSeps[] = "/\\";

bool isSep( char c ) { return c == '/' || c == '\\'; }

// The one piece of state in this file, and only so the self-tests can check both
// behaviours on one host: which platform folds case in file names is not something
// a machine can discover at runtime without writing a file to prove it.
bool &caseFoldSetting()
{
    static bool folded
#if defined(_WIN32) || defined(__MSDOS__) || defined(__OS2__)
        = true;
#else
        = false;
#endif
    return folded;
}

bool sameText( const std::string &a, const std::string &b )
{
    if( !caseFoldSetting() ) return a == b;
    if( a.size() != b.size() ) return false;
    for( std::size_t i = 0; i < a.size(); ++i )
    {
        if( std::tolower( (unsigned char) a[i] ) !=
            std::tolower( (unsigned char) b[i] ) ) return false;
    }
    return true;
}

// The part of a path that anchors it to a device: "", "/", "C:", "C:/", or
// "//host/share". It is kept separate because it is not comparable as a name:
// `..` does not walk out of a share, and `C:\a` next to `D:\a` is not one path
// differing by case but two different disks.
std::string anchorOf( const std::string &path )
{
    if( path.empty() ) return std::string();

    if( isSep( path[0] ) && path.size() > 1 && path[1] == path[0] )
    {
        // `\\host\share\dir`, spelled with forward slashes here so a path typed
        // with either separator anchors the same way. Host and share together:
        // `\\srv\secrets-2` is not `\\srv\secrets`.
        std::vector< std::string > names;
        std::size_t at = 2;
        for( int part = 0; part < 2 && at < path.size(); ++part )
        {
            while( at < path.size() && isSep( path[at] ) ) ++at;
            if( at >= path.size() ) break;
            const std::size_t end = path.find_first_of( kSeps, at );
            names.push_back( ( end == std::string::npos )
                ? path.substr( at ) : path.substr( at, end - at ) );
            at = ( end == std::string::npos ) ? path.size() : end + 1;
        }

        std::string out = "//";
        for( std::size_t i = 0; i < names.size(); ++i )
        {
            if( i ) out += "/";
            out += names[i];
        }
        return out;
    }
    if( path.size() > 1 && path[1] == ':' &&
        std::isalpha( (unsigned char) path[0] ) )
    {
        // `C:\dir` anchors as "C:/". `C:file` anchors as "C:", meaning "wherever
        // drive C currently is" - a fact only drive C has.
        const std::string drive = path.substr( 0, 2 );
        return ( path.size() > 2 && isSep( path[2] ) ) ? drive + "/" : drive;
    }
    if( isSep( path[0] ) ) return "/";
    return std::string();
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

bool canFlushToDisk()
{
#if defined(_WIN32)
    return true;                        // _commit()
#elif defined(__unix__) || defined(__APPLE__)
    return true;                        // fsync()
#else
    return false;
#endif
}

void flushToDisk( std::FILE *file )
{
    if( !file ) return;
    // The stream's own buffer first: everything below only knows about what the
    // operating system was given. Ignored results are deliberate - there is no
    // action for a failure here, and `fclose` reports what matters about the file.
    (void) std::fflush( file );
#if defined(_WIN32)
    (void) _commit( _fileno( file ) );
#elif defined(__unix__) || defined(__APPLE__)
    (void) ::fsync( ::fileno( file ) );
#endif
}

std::string pathAnchor( const std::string &path )
{
    return anchorOf( path );
}

bool isAbsolutePath( const std::string &path )
{
    return !anchorOf( path ).empty();
}

bool isUncPath( const std::string &path )
{
    return path.size() > 1 && isSep( path[0] ) && path[1] == path[0];
}

bool isPathRoot( const std::string &path )
{
    const std::string anchor = anchorOf( path );
    if( anchor.empty() ) return false;
    // Whatever follows the anchor has to be separators and nothing else: "/" and
    // "C:\" are roots, "/usr" and "C:\proj" are not.
    for( std::size_t i = anchor.size(); i < path.size(); ++i )
        if( !isSep( path[i] ) ) return false;
    return true;
}

bool pathIsUnder( const std::string &child, const std::string &parent )
{
    const std::string childAnchor = anchorOf( child );
    const std::string parentAnchor = anchorOf( parent );
    if( childAnchor.empty() || parentAnchor.empty() ) return false;
    if( !sameText( childAnchor, parentAnchor ) ) return false;

    // Component by component, so that a name which merely starts like the parent
    // is not mistaken for being inside it.
    std::size_t i = parentAnchor.size(), j = childAnchor.size();
    for( ;; )
    {
        while( i < parent.size() && isSep( parent[i] ) ) ++i;
        while( j < child.size() && isSep( child[j] ) ) ++j;
        const std::size_t nextParent = parent.find_first_of( kSeps, i );
        const std::size_t nextChild  = child.find_first_of( kSeps, j );
        const std::string parentPart = ( nextParent == std::string::npos )
            ? parent.substr( i ) : parent.substr( i, nextParent - i );
        const std::string childPart = ( nextChild == std::string::npos )
            ? child.substr( j ) : child.substr( j, nextChild - j );

        if( parentPart.empty() ) return true;    // parent ran out: at or below it
        if( childPart.empty() ) return false;    // child ran out first: /u/proj in /u/p
        if( !sameText( parentPart, childPart ) ) return false;

        i = ( nextParent == std::string::npos ) ? parent.size() : nextParent;
        j = ( nextChild == std::string::npos ) ? child.size() : nextChild;
    }
}

bool pathsAreCaseInsensitive()
{
    return caseFoldSetting();
}

void setPathsAreCaseInsensitive( bool value )
{
    caseFoldSetting() = value;
}

} // namespace atui::core
} // namespace atui
