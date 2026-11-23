#include "harness.h"
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#if defined( _WIN32 )
#include <direct.h>       // _getcwd, for current_dir()
#else
#include <unistd.h>       // getcwd
#endif

namespace atui_test {

namespace {
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
    int passed = 0;
    int failed = 0;

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

        if( r == 0 ) {
            ++passed;
            std::fprintf( stderr, "self-test PASS %s\n", e.name );
        } else {
            ++failed;
            std::fprintf( stderr, "self-test FAIL %s (rc=%d)\n", e.name, r );
        }
    }

    std::fprintf( stderr, "self-test summary: %d passed, %d failed\n", passed, failed );
    return ( failed == 0 ) ? 0 : 1;
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
