// Can this toolchain link and run a program at all?
//
// Every build so far stopped at an object file, which leaves the questions a
// program actually depends on unanswered: whether `system os2v2` makes a
// character-mode executable a telnet session can start, whether the libraries
// wlink picks by itself satisfy a C++ program's startup, whether static objects
// get their constructors run before main, and how large the result comes out --
// size is one of atui's requirements, and it should be measured from the
// smallest interesting program rather than discovered at the end.
//
//   build.cmd link hello.cpp hello.exe
//   hello.exe
//
// Nothing in here is a test atui keeps; it is a question to the toolchain, kept
// because the answer is the foundation everything else is stacked on.

#include <cstdio>
#include <cstring>
#include <string>

namespace {

// Constructor runs before main; if the number printed is wrong, startup never
// ran it and anything relying on a global's value will be reading zero.
struct BeforeMain
{
    int seen;
    BeforeMain() : seen( 41 ) {}
} g_beforeMain;

} // namespace

int main( int argc, char **argv )
{
    std::string greeting( "hello os/2" );
    std::printf( "%s: argv[0] is %d bytes, static ctor saw %d\n",
                 greeting.c_str(),
                 argc > 0 && argv && argv[0] ? (int) std::strlen( argv[0] ) : -1,
                 g_beforeMain.seen + 1 );
    return 0;
}
