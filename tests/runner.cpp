#include "harness.h"
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#if defined( _WIN32 )
#include <direct.h>       // _getcwd, for current_dir()
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>      // CreateSymbolicLinkA, for make_link()
#else
#include <unistd.h>       // getcwd
#include <sys/stat.h>     // mkdir, for makeDir()
#endif

namespace atui_test {

namespace {
    // What skip_test() returns, and the reason it gives.  The value is deliberately
    // not a plausible failure code: a test that fails for real has to keep looking
    // like a failure, and one that reports a skip has to look like neither.
    const int kSkipped = 23577;
    const char *g_skip_why = 0;

    // A fixed array rather than a container, because this file is compiled into
    // the C++98 build as well and has to work in a Watcom program with no
    // allocator to spare.  The number is generous and going over it is an error
    // the run reports, not something it quietly absorbs: with a silent cap the
    // summary says "all passed" about tests that never ran, which is the one thing
    // a test harness must not do.  (It happened at 128 -- ten tests were added, the
    // total stayed at 128, and four markdown tests stopped existing.)
    enum { kMaxTests = 512 };

    struct Entry {
        const char *name;
        int (*fn)( void );
    };

    Entry m_entries[ kMaxTests ];
    int m_count = 0;
    int m_dropped = 0;
}

void register_test( const char *name, int (*fn)( void ) )
{
    if( m_count < kMaxTests )
        m_entries[ m_count++ ] = { name, fn };
    else
        ++m_dropped;      // reported by run(), which then fails
}

int run()
{
    // What this binary was configured with, before anything about it passes or fails.
    // The summary's number cannot be read without it: the same suite is 208 tests with
    // the transport compiled in and 152 without, and a clean result off a build that
    // never compiled the network is not evidence about the network.
#if defined( ATUI_HAVE_NETWORK )
    const char *const net = "on";
#else
    const char *const net = "off";
#endif
    std::fprintf( stderr, "self-test build: network=%s, %d tests registered\n",
                  net, m_count );

    int passed = 0;
    int failed = 0;
    int skipped = 0;

    if( m_dropped > 0 )
    {
        std::fprintf( stderr,
                      "self-test: %d test(s) were NEVER REGISTERED: the harness "
                      "holds %d. Raise kMaxTests in tests/runner.cpp -- a run that "
                      "silently omits tests reports success about work it did not "
                      "do.\n", m_dropped, kMaxTests );
        ++failed;
    }

    for( int i = 0; i < m_count; ++i ) {
        const Entry &e = m_entries[ i ];
        int r = 0;
        try {
            r = e.fn();
        } catch( const std::exception &s ) {
            std::fprintf( stderr, "self-test %s threw: %s\n", e.name, s.what() );
            r = -1;
        } catch( ... ) {
            std::fprintf( stderr, "self-test %s threw (unknown type)\n", e.name );
            r = -1;
        }

        if( r == kSkipped ) {
            ++skipped;
            std::fprintf( stderr, "self-test SKIP %s - %s\n", e.name,
                          g_skip_why ? g_skip_why : "no reason given" );
            g_skip_why = 0;
        } else if( r == 0 ) {
            ++passed;
            std::fprintf( stderr, "self-test PASS %s\n", e.name );
        } else {
            ++failed;
            std::fprintf( stderr, "self-test FAIL %s (rc=%d)\n", e.name, r );
        }
    }

    // Skips are counted in the line people read, not filed away: the failure mode
    // this whole mechanism exists to avoid is a suite that has quietly stopped
    // checking something and still reports 200 passed.
    std::fprintf( stderr, "self-test summary: %d passed, %d failed, %d skipped\n",
                  passed, failed, skipped );
    return ( failed == 0 ) ? 0 : 1;
}

int skip_test( const char *why )
{
    g_skip_why = why;
    return kSkipped;
}

bool make_link( const std::string &target, const std::string &link, bool directory )
{
#if defined( _WIN32 )
    // SYMBOLIC_LINK_FLAG_DIRECTORY is 1, and the kind is fixed when the link is
    // made -- unlike POSIX, where a link is just a link.  Windows 10 lets an
    // unprivileged process do this only with developer mode or the privilege; where
    // it says no, the caller skips, because a junction would not test the file case.
    return ::CreateSymbolicLinkA( link.c_str(), target.c_str(),
                                  directory ? 1 : 0 ) != 0;
#else
    (void) directory;
    return ::symlink( target.c_str(), link.c_str() ) == 0;
#endif
}

void set_env( const char *name, const char *value )
{
#if defined( _WIN32 )
    // No setenv() on Windows; _putenv_s takes the pair and copies it, which is
    // all a test needs.  <stdlib.h> rather than <cstdlib> because the function
    // is a POSIX-named extension that <cstdlib> does not promise.
    _putenv_s( name, value );
#else
    ::setenv( name, value, 1 );
#endif
}

void unset_env( const char *name )
{
#if defined( _WIN32 )
    // Setting a variable to the empty string removes it on Windows.  It is not
    // the same as setting it to "": there is no way to say "present but empty"
    // here, which a test that cared about the difference would have to ask
    // about differently -- none does.
    _putenv_s( name, "" );
#else
    ::unsetenv( name );
#endif
}

std::string temp_dir()
{
    // TMPDIR is the POSIX name, TMP and TEMP the Windows ones; Windows sets the
    // latter two and has no /tmp, so the order matters and none of them may be
    // set at all.  The trailing separators come off because every caller adds
    // its own, and "C:\Users\stash\AppData\Local\Temp\/\/x" is a path Windows
    // accepts but nobody reading a failure would want to see.
    const char *d = std::getenv( "TMPDIR" );
    if( !d || !*d ) d = std::getenv( "TEMP" );
    if( !d || !*d ) d = std::getenv( "TMP" );
    if( !d || !*d ) d = "/tmp";
    std::string s( d );
    while( s.size() > 1 && ( s[ s.size() - 1 ] == '/' || s[ s.size() - 1 ] == '\\' ) )
        s.erase( s.size() - 1 );
    return s;
}

std::string current_dir()
{
    char buf[ 4096 ];
#if defined( _WIN32 )
    if( ::_getcwd( buf, sizeof buf ) ) return std::string( buf );
#else
    if( ::getcwd( buf, sizeof buf ) ) return std::string( buf );
#endif
    (void) buf;
    return std::string();
}

// The two directory calls, named once.  `<direct.h>` owns them on MSVC as `_mkdir`
// and `_rmdir`, and `_mkdir` takes no mode -- which a suite that spells the POSIX name
// at each of its eight call sites only discovers by not building there, and a suite
// that does not build is a suite that is not tested.  The mode is 0755 everywhere it
// is asked for, so nothing is lost by fixing it here.
int makeDir( const std::string &path )
{
#if defined( _WIN32 )
    return ::_mkdir( path.c_str() );
#else
    return ::mkdir( path.c_str(), 0755 );
#endif
}

int removeDir( const std::string &path )
{
#if defined( _WIN32 )
    return ::_rmdir( path.c_str() );
#else
    return ::rmdir( path.c_str() );
#endif
}

std::string wait_command( long seconds )
{
    char buf[ 64 ];
#if defined( _WIN32 )
    // One second per echo, and the target address keeps it from needing a
    // network.  `>nul` because the replies are not the point and they would
    // otherwise arrive as the output under test.
    std::snprintf( buf, sizeof buf, "ping -n %ld 127.0.0.1 >nul", seconds + 1 );
#else
    std::snprintf( buf, sizeof buf, "sleep %ld", seconds );
#endif
    return std::string( buf );
}

std::string stdin_command()
{
    // Both read stdin to end-of-file and print nothing when there is nothing
    // there, which is the behaviour under test: atui gives a command no input, and
    // a command that waits for input has to be told there is none rather than
    // left waiting for a person who is not coming.
#if defined( _WIN32 )
    return std::string( "more" );
#else
    return std::string( "cat" );
#endif
}

bool set_read_only( const std::string &path, bool readOnly )
{
#if defined( _WIN32 )
    // The attribute itself, which is what a user's `attrib +R` sets and what
    // `isWritableFile()`'s `_access(path, 2)` then reports as EACCES -- measured, both
    // halves.  The CRT's POSIX-shaped `_chmod` is not what this wants even where it
    // exists: it is a translation layer over the attribute, and the file is one bit.
    const DWORD attr = GetFileAttributesA( path.c_str() );
    if( attr == INVALID_FILE_ATTRIBUTES )
        return false;
    const DWORD want = readOnly ? ( attr | FILE_ATTRIBUTE_READONLY )
                                : ( attr & ~FILE_ATTRIBUTE_READONLY );
    return SetFileAttributesA( path.c_str(), want ) != 0;
#else
    const mode_t keep = S_IRUSR | S_IRGRP | S_IROTH;
    const mode_t mode = readOnly ? keep : ( keep | S_IWUSR );
    return ::chmod( path.c_str(), mode ) == 0;
#endif
}

void note( const std::string &what )
{
    std::fprintf( stderr, "self-test NOTE %s\n", what.c_str() );
    std::fflush( stderr );
}

std::string make_file_command( const std::string &path )
{
#if defined( _WIN32 )
    // Redirecting nothing into the name is cmd's `touch`: an empty file, created, and
    // nothing on stdout.  The tests ask only whether the file a command promised
    // appeared, so what matters is the quoting, not the verb.
    return std::string( "type nul > \"" ) + path + "\"";
#else
    return std::string( "touch '" ) + path + "'";
#endif
}

std::string in_dir_command( const std::string &dir, const std::string &command )
{
    // `&&` means the same thing in both, which is the one part of a command line here
    // that does.
#if defined( _WIN32 )
    return std::string( "cd \"" ) + dir + "\" && " + command;
#else
    return std::string( "cd '" ) + dir + "' && " + command;
#endif
}

std::string background_wait_command( long seconds )
{
    char buf[ 256 ];
#if defined( _WIN32 )
    // `start /b` is cmd's background, and the grandchild is another cmd running
    // ping: a separate process, holding the same handles.  Quoting note -- the
    // inner quotes are what `cmd /s /c "..."` hands to cmd as part of the command,
    // and they survive it (measured).
    std::snprintf( buf, sizeof buf,
                   "start /b cmd /c \"ping -n %ld 127.0.0.1 >nul\" "
                   "& ping -n %ld 127.0.0.1 >nul",
                   seconds + 1, seconds + 1 );
#else
    std::snprintf( buf, sizeof buf, "sleep %ld & sleep %ld", seconds, seconds );
#endif
    return std::string( buf );
}

std::string json_str( const std::string &s )
{
    std::string out;
    out.reserve( s.size() + 8 );
    for( std::size_t i = 0; i < s.size(); ++i )
    {
        const char c = s[i];
        switch( c )
        {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

} // namespace atui_test
