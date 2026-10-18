/* Capability probe: what does an era C++ compiler's library actually give us?

   Written in deliberate, boring C++98.  Whatever fails to *compile* here is a
   port blocker and the compiler's own message is the result we want; whatever
   compiles, the program then exercises at run time and prints one fact per
   line, because the answer arrives over a telnet screen and has to be readable
   without a diff.

   Build (Open Watcom, OS/2):

     wpp386 -bt=os2 -i=compat\os2\include -fo=probe_cxx.obj probe_cxx.cpp
     wlink system os2v2 name probe_cxx.exe file probe_cxx.obj

   The -i= is what makes <algorithm> resolve; see compat/os2/include.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* long names: only reachable through the include forwarders */
#include <algorithm>
#include <exception>
#include <functional>
#include <stdexcept>
#include <streambuf>
#include <type_traits>

/* names short enough to exist as-is */
#include <fstream>
#include <iosfwd>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
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
            (int)sizeof( intptr_t ), (int)sizeof( uintmax_t ),
            (unsigned long)SIZE_MAX, INT_MAX );

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
    check( "std::string substr/find/erase",
           s.substr( 4 ) == "os2" && s.find( '-' ) == 4 && (s.erase( 0, 5 ), s == "os2") );

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

    /* ---- streams ----------------------------------------------------- */
    std::ostringstream os;
    os << "n=" << 42 << " hex=" << std::hex << 255 << std::dec << " f=" << 3.5;
    check( "ostringstream", os.str() == "n=42 hex=ff f=3.5" );

    const char *tmp = "probe_cxx.tmp";
    {
        std::ofstream out( tmp, std::ios::binary | std::ios::trunc );
        out << "abc" << 12 << "\n";
    }
    {
        std::ifstream in( tmp, std::ios::binary );
        std::string got( (std::istreambuf_iterator<char>( in )),
                          std::istreambuf_iterator<char>() );
        check( "fstream binary round trip", got == "abc12\n" );
    }
    remove( tmp );
    check( "remove()", !std::ifstream( tmp ).is_open() );

    std::istringstream is( "11 22" );
    int a = 0, b = 0;
    is >> a >> b;
    check( "istringstream extract", a == 11 && b == 22 );

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
        std::string huge;
        huge.reserve( (size_t)-1 );   // wants to fail, but is allowed to succeed
        throw std::bad_alloc();
    } catch( const std::bad_alloc & ) {
        caught = true;
    }
    check( "catch std::bad_alloc", caught );

    check( "typeid().name()", typeid( std::string ).name() != 0 );

    struct Base { virtual ~Base() {} };
    struct Derived : Base { int x; };
    Base *asBase = new Derived;
    check( "dynamic_cast", dynamic_cast<Derived *>( asBase ) != 0 );
    delete asBase;

    /* ---- smart pointers: only the C++98 one is expected -------------- */
    std::auto_ptr<int> ap( new int( 5 ) );
    check( "std::auto_ptr", *ap == 5 );

    /* ---- callbacks, the way the UI layer needs them ------------------- */
    int ( *fn )( int, int ) = &byAbs;
    check( "pointer to a comparison function", fn( -1, 2 ) < 0 );

    printf( "== result ==\n  %d checks, %d failed\n", g_checks, g_failed );
    return g_failed == 0 ? 0 : 1;
}
