/* Capability probe: what does an era C++ compiler's library actually give us?

   Written in deliberate, boring C++98.  Whatever fails to *compile* here is a
   port blocker and the compiler's own message is the result we want; whatever
   compiles, the program then exercises at run time and prints one fact per
   line, because the answer arrives over a telnet screen and has to be readable
   without a diff.

   Build (Open Watcom, OS/2):

     wpp386 -bt=os2 -xs -fo=probe_cxx.obj probe_cxx.cpp
     wlink system os2v2 name probe_cxx.exe file probe_cxx.obj

   -xs is required: exceptions are off by default, and the symptom is not a
   message about exceptions but errors from inside Watcom's own <string>.

   Note there is no <streambuf> here, unlike earlier revisions of this file.
   Watcom's own streambuf header cannot be included on its own -- it uses
   streampos and streamoff without defining them or including what does, and
   returns twenty errors from inside the compiler's installation -- and nothing
   can be placed on the include path to fix it, because Watcom's h directory is
   searched before any path you set. Anything that wants std::streambuf includes
   <fstream>, which is self-contained. See compat/os2/include/README.md.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* long names, to show the compiler resolves them unaided */
#include <algorithm>
#include <exception>
#include <functional>
#include <stdexcept>
#include <type_traits>

/* names short enough to exist as-is */
#include <fstream>
#include <iosfwd>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <strstream>
#include <utility>
#include <vector>

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#define STR2(x) #x
#define STR(x) STR2(x)
#define YESNO(x) ((x) ? "yes" : "NO")

static int g_checks = 0;
static int g_failed = 0;

static void check( const char *what, bool okay )
{
    printf( "  %-34s %s\n", what, okay ? "ok" : "FAILED" );
    ++g_checks;
    if( !okay )
        ++g_failed;
}

/* a comparison function for std::sort, to prove <algorithm> really works
   rather than merely parsing */
static int byAbs( int a, int b )
{
    return (a < 0 ? -a : a) < (b < 0 ? -b : b);
}

/* RTTI is off by default, as exceptions are, and asking for typeid or
   dynamic_cast without -xr is a compile error rather than a runtime one.
   Guarded on __RTTI__, the macro -xr defines, so the probe runs either way and
   says which way it ran.

   These two structs are at file scope rather than inside main, where the
   equivalent code used to sit: that is legal C++ and this compiler rejects it
   with a bare syntax error, which is the usual trouble with a vtable that would
   have to be emitted in function scope. */
#if defined( __RTTI__ )
struct ProbeBase { virtual ~ProbeBase() {} };
struct ProbeDerived : ProbeBase { int x; };
#endif

int main( void )
{
    /* ---- what the compiler says about itself -------------------------- */
    printf( "== compiler ==\n" );
    printf( "  __WATCOMC__=%s  __cplusplus=%s\n", STR( __WATCOMC__ ), STR( __cplusplus ) );
#ifdef __WATCOM_CPLUSPLUS__
    printf( "  __WATCOM_CPLUSPLUS__=%s\n", STR( __WATCOM_CPLUSPLUS__ ) );
#endif
#ifdef __OS2__
    printf( "  __OS2__=%s\n", STR( __OS2__ ) );
#endif
#ifdef __386__
    printf( "  __386__ defined\n" );
#endif
#ifdef __FLAT__
    printf( "  __FLAT__=%s\n", STR( __FLAT__ ) );
#endif
#ifdef __RTTI__
    printf( "  __RTTI__=%s\n", STR( __RTTI__ ) );
#endif
#ifdef __EXCEPTIONS__
    printf( "  __EXCEPTIONS__=%s\n", STR( __EXCEPTIONS__ ) );
#endif

    /* ---- representation --------------------------------------------- */
    printf( "== sizes ==\n" );
    printf( "  char=%d short=%d int=%d long=%d long long=%d\n",
            (int)sizeof( char ), (int)sizeof( short ), (int)sizeof( int ),
            (int)sizeof( long ), (int)sizeof( long long ) );
    printf( "  void*=%d size_t=%d ptrdiff_t=%d wchar_t=%d\n",
            (int)sizeof( void* ), (int)sizeof( size_t ),
            (int)sizeof( ptrdiff_t ), (int)sizeof( wchar_t ) );
    printf( "  float=%d double=%d long double=%d\n",
            (int)sizeof( float ), (int)sizeof( double ), (int)sizeof( long double ) );
    printf( "  intptr_t=%d uintmax_t=%d SIZE_MAX=%lu INT_MAX=%d\n",
            /* SIZE_MAX is not defined by this <stdint.h>, measured, so the
               limit comes from numeric_limits, which is where the port should
               get it anyway. */
            (int)sizeof( intptr_t ), (int)sizeof( uintmax_t ),
            (unsigned long)std::numeric_limits<size_t>::max(), INT_MAX );

    /* ---- printf: do the length modifiers work? ------------------------ */
    /* A pre-C99 runtime prints "z" or "ll" literally here, which silently
       corrupts diagnostics and any number formatting that leans on them. */
    printf( "== printf ==\n" );
    printf( "  %%zu -> [%zu] (want 1234)\n", (size_t)1234 );
    printf( "  %%lld -> [%lld] (want 1234)\n", (long long)1234 );
    printf( "  %%llu -> [%llu] (want 18446744073709551615)\n", (unsigned long long)~0ULL );
    printf( "  %%p -> [%p]\n", (void*)&g_checks );
    char buf[64];
    int n = snprintf( buf, sizeof( buf ), "snprintf:%d", 7 );
    printf( "  snprintf -> [%s] returned %d\n", buf, n );

    /* ---- containers and strings -------------------------------------- */
    printf( "== library ==\n" );
    std::string s = "atui";
    s += "-os2";
    check( "std::string append/compare", s == "atui-os2" && s.size() == 8 );
    /* One fact per line, because the first version folded substr, find and
       erase into a single conjunction and could only report that something
       inside it was wrong, which is not a measurement.  The values are printed
       as well: over a telnet link a difference has to be readable, not deduced. */
    {
        std::string head = s.substr( 0, 4 );
        std::string tail = s.substr( 4 );
        check( "std::string substr( 0, 4 )", head == "atui" );
        /* "-os2", not "os2": position 4 of "atui-os2" is the hyphen.  This
           check asked for the wrong thing twice before the values were printed
           next to it, which is the whole reason they are printed here. */
        check( "std::string substr( 4 )", tail == "-os2" );
        printf( "       substr(0,4)=[%s]  substr(4)=[%s]\n", head.c_str(), tail.c_str() );
        check( "std::string find", s.find( '-' ) == 4 );
        std::string cut = s;
        cut.erase( 0, 5 );
        check( "std::string erase( 0, 5 )", cut == "os2" );
        printf( "       after erase=[%s]\n", cut.c_str() );
    }

    std::vector<int> v;
    v.push_back( -3 );
    v.push_back( 2 );
    v.push_back( -1 );
    std::sort( v.begin(), v.end(), byAbs );
    check( "std::sort with a function", v[0] == -1 && v[1] == 2 && v[2] == -3 );
    v.erase( v.begin() );
    check( "std::vector erase", v.size() == 2 );

    std::map<std::string, int> m;
    m["one"] = 1;
    m["two"] = 2;
    check( "std::map keyed by string", m["one"] == 1 && m.size() == 2
                                       && m.find( "two" )->second == 2 );

    std::set<int> set1;
    set1.insert( 3 );
    set1.insert( 3 );
    check( "std::set", set1.size() == 1 );

    std::list<std::string> l;
    l.push_back( "b" );
    l.push_front( "a" );
    check( "std::list", l.front() == "a" && l.back() == "b" );

    std::vector<std::string> words;
    words.push_back( "beta" );
    words.push_back( "alpha" );
    std::sort( words.begin(), words.end() );
    check( "std::sort of strings", words[0] == "alpha" );

    /* ---- streams ------------------------------------------------------

       No <sstream> here, and none can be arranged: that header is an empty stub
       in this library -- after its guards, its whole content is
       `namespace std { }` -- so there is no ostringstream to name, and a compat
       header cannot supply one because Watcom's own header wins every include
       lookup.  strstream is the pre-standard string stream that <sstream>
       eventually replaced, and it is real here, so it is what the port's
       formatting helper will be built on -- or snprintf, measured below, which
       is smaller and needs no stream object at all.

       istreambuf_iterator is absent as well, so the file comes back a character
       at a time. */
    {
        char buf[64];
        std::ostrstream os( buf, sizeof( buf ) );
        os << "n=" << 42 << " hex=" << std::hex << 255 << std::dec
           << " f=" << 3.5 << std::ends;
        check( "ostrstream formatting", std::string( buf ) == "n=42 hex=ff f=3.5" );

        std::istrstream is( "7 8", 3 );
        int a = 0, b = 0;
        is >> a >> b;
        check( "istrstream parsing", a == 7 && b == 8 );
    }

    {
        char buf[64];
        /* unqualified: <cstdio> here declares _snprintf and snprintf, and puts
           neither in namespace std, which is the same trap as memcpy */
        snprintf( buf, sizeof( buf ), "n=%d hex=%x f=%.1f", 42, 255, 3.5 );
        check( "snprintf", std::string( buf ) == "n=42 hex=ff f=3.5" );
    }

    const char *tmp = "probe_cxx.tmp";
    {
        std::ofstream out( tmp, std::ios::binary | std::ios::trunc );
        out << "abc" << 12 << "\n";
    }
    {
        std::ifstream in( tmp, std::ios::binary );
        std::string got;
        char ch;
        while( in.get( ch ) )
            got += ch;
        check( "fstream binary round trip", got == "abc12\n" );
    }
    remove( tmp );
    check( "remove()", !std::ifstream( tmp ).is_open() );

    /* ---- iostream: present, and at what cost ------------------------- */
    /* Kept to one line so it can be removed if the static-link cost of the
       iostream machinery ever matters. */
    std::cout << "== iostream works ==" << std::endl;

    /* ---- exceptions and RTTI ----------------------------------------- */
    std::string message;
    try {
        throw std::runtime_error( "boom" );
    } catch( const std::exception &e ) {
        message = e.what();
    }
    check( "throw std::runtime_error / catch", !message.empty() );

    bool caught = false;
    try {
        /* Not called `huge`: that is a memory-class keyword in this compiler,
           alongside far and near, so `std::string huge;` reads as a declaration
           with a storage class and no name, and the error is a bare syntax
           error several columns away from the cause.  */
        std::string enormous;
        enormous.reserve( (size_t)-1 );   // wants to fail, may legitimately succeed
        throw std::bad_alloc();
    } catch( const std::bad_alloc & ) {
        caught = true;
    }
    check( "catch std::bad_alloc", caught );

#if defined( __RTTI__ )
    check( "typeid().name()", typeid( std::string ).name() != 0 );
    ProbeBase *asBase = new ProbeDerived;
    check( "dynamic_cast", dynamic_cast<ProbeDerived *>( asBase ) != 0 );
    delete asBase;
#else
    printf( "  rtti: off, so typeid and dynamic_cast are not compiled "
            "(pass -xr to enable)\n" );
#endif

    /* ---- smart pointers: only the C++98 one is expected -------------- */
    std::auto_ptr<int> ap( new int( 5 ) );
    check( "std::auto_ptr", *ap == 5 );

    /* ---- callbacks, the way the UI layer needs them ------------------- */
    int ( *fn )( int, int ) = &byAbs;
    /* byAbs(a, b) is |a| < |b|, so byAbs( -1, 2 ) is 1 rather than a negative
       comparison result: the check below used to ask for "< 0", which was the
       probe being wrong about the helper two screens above it.  Worth keeping
       in this form, because taking a function pointer is exactly what the UI
       layer does for callbacks. */
    check( "pointer to a comparison function", fn( -1, 2 ) == 1 && fn( 5, 2 ) == 0 );

    printf( "== result ==\n  %d checks, %d failed\n", g_checks, g_failed );
    return g_failed == 0 ? 0 : 1;
}
