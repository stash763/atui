// Which of the <cxxx> headers does Watcom 1.9 actually fill in?
//
// atui's portable core writes std::size_t, std::strlen, std::snprintf, std::FILE
// and about twenty more names from the C library, qualified, because that is
// what the standard promises a <cxxx> header gives you.  libstdc++ also leaks
// them into the global namespace, so a bare spelling works there too and the
// difference never shows.  Watcom is the compiler that finds out: it refused
// bare `getenv` with <cstdlib> (E029, seen compiling src/tui/theme.cpp), which
// is conforming behaviour and means the qualified spelling is the one that has
// to be used throughout.
//
// But which of the qualified ones are there at all?  Watcom is known to ship
// <sstream>, <locale>, <queue> and <valarray> as empty stubs, so "include the
// header and use the name" is not something to assume here.  This file names
// every C-library name atui uses, so the answer comes back in one compile
// rather than one per file that happens to use it:
//
//   build.cmd one tools\os2\probe_stdhdrs.cpp
//
// Whatever the log says is the port's to-do list, and if a name is missing the
// place to say so is compat/os2/include, which already forwards the two headers
// Watcom lacks entirely.
//
// The answer (2026-10-19, `probe_stdhdrs.cpp: 116 lines, ..., no warnings, no
// errors`): every qualified name below is there, including std::size_t,
// std::FILE, std::snprintf and std::strftime.  The only names that cannot be
// qualified are the va_* macros, which are macros in any implementation.
// So atui's std:: spellings are correct and the bare ones are what have to
// change.
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

int probe_cctype()
{
    return std::isspace( ' ' ) + std::isalnum( 'a' ) + std::isdigit( '1' )
         + std::isalpha( 'a' ) + std::ispunct( '.' ) + std::isxdigit( 'f' )
         + std::tolower( 'A' ) + std::toupper( 'a' );
}

int probe_cstdio()
{
    char buf[32];
    std::FILE *f = 0;
    int n = std::sprintf( buf, "%d", 1 );
    n += std::snprintf( buf, sizeof( buf ), "%d", 2 );
    n += std::printf( "%s", buf );
    if( f ) std::fprintf( f, "x" );
    std::fflush( 0 );
    std::fputs( "x", stdout );
    n += (int) std::fwrite( "abc", 1, 3, stdout );
    std::fgetc( stdin );
    return n;
}

int probe_cstdlib()
{
    char *end = 0;
    char *p = (char *) std::malloc( 8 );
    std::free( p );
    if( std::getenv( "ATUI_THEME" ) ) return 1;
    return std::atoi( "1" ) + (int) std::atol( "2" )
         + (int) std::strtol( "3", &end, 10 ) + std::abs( -4 );
}

int probe_cstring()
{
    char buf[8];
    std::memcpy( buf, "abc", 4 );
    std::memset( buf, 0, sizeof( buf ) );
    std::memmove( buf, "xy", 2 );
    std::strcmp( "a", "b" );
    std::strncmp( "a", "b", 1 );
    std::strcpy( buf, "z" );
    std::strncpy( buf, "z", 3 );
    std::strchr( buf, 'z' );
    std::strstr( buf, "z" );
    return (int) std::strlen( buf ) + std::memcmp( buf, buf, 1 );
}

int probe_ctime()
{
    std::time_t now = std::time( 0 );
    std::tm t;
    t.tm_year = 0;
    t.tm_mon = 0;
    t.tm_mday = 1;
    std::strftime( (char *) &t, 1, "%Y", &t );
    return now > 0 ? 1 : 0;
}

std::size_t probe_cstddef()
{
    std::size_t n = sizeof( int );
    return n;
}

void probe_handler( int ) {}

int probe_csignal()
{
    std::signal( SIGINT, probe_handler );
    std::raise( SIGINT );
    return errno;   // errno is a macro, so it is not namespaced anywhere
}

void probe_cstdarg( int n, ... )
{
    // va_start, va_arg and va_end are macros, so they cannot carry a
    // namespace: `std::va_start( ... )` is E777 "expecting 'id' after '::'",
    // which is what the first run of this probe reported.  va_list is a type,
    // and that one can.
    std::va_list ap;
    va_start( ap, n );
    va_arg( ap, int );
    va_end( ap );
}
