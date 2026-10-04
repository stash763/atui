// atui main entry point.
//
// Dispatches:
//   atui --self-test   run the C++ self-test suite and exit
//   atui --version     print the version and exit
//   atui               launch the TUI application

#include <cstring>
#include <cstdio>

#include "tui/app.h"
#include "harness.h"

int main( int argc, char **argv )
{
    if( argc > 1 )
    {
        if( std::strcmp( argv[1], "--self-test" ) == 0 )
        {
            atui_test::init();
            return atui_test::run();
        }

        if( std::strcmp( argv[1], "--version" ) == 0 )
        {
            std::printf( "atui %s\n", ATUI_VERSION );
            return 0;
        }
    }

    return atui::tui::run_app();
}
