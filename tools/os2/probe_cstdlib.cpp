// Does Watcom's <cstdlib> declare getenv, and in which namespace?
//
// atui calls it both ways -- `std::getenv` in settings.cpp, bare `getenv` in
// provider.cpp and theme.cpp -- and libstdc++ lets both compile because it
// declares the C names in the global namespace as well as in std.  Open Watcom
// 1.9 says E029 "symbol 'getenv' has not been declared" for the bare spelling
// with <cstdlib> (measured, compiling src/tui/theme.cpp), which is what a
// conforming <cstdlib> is entitled to do.
//
// This file is that measurement narrowed to the useful half: <cstdlib> alone,
// the standard spelling.  If it compiles, the rule for atui is to include
// <cstdlib> and write std::getenv, and the files that call it bare have to say
// std:: too.
//
//   build.cmd one tools\os2\probe_cstdlib.cpp
#include <cstdlib>

int env_is_set()
{
    return std::getenv( "ATUI_THEME" ) != 0;
}
