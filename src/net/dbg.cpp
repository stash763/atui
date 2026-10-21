#include "dbg.h"

// Guarded because a build without debug keys has nothing to lock, and taking a
// semaphore per log line in the build that ships is not something to do by accident.
// (`<mutex>` does reach Open Watcom -- compat/os2/include supplies it over OS/2 mutex
// semaphores, which is why approval.cpp compiles there; what that directory does not
// supply is <thread>, see PORTING.md.  But a debug log is a host-build feature, and
// the fewer C++11 headers a C++98 translation unit pulls in the fewer there are to
// reason about.)
#ifdef ATUI_DEBUG_KEYS
#include <mutex>
#endif

namespace atui {
namespace net {

void dbg( const char *fmt, ... )
{
#ifdef ATUI_DEBUG_KEYS
    // Both statics are inside the ifdef: with debugging off there is nothing
    // here to open and nothing to lock, and an unused variable is a warning.
    static FILE *f = 0;
    static std::mutex m;
    if( !f )
        f = fopen( "/tmp/atui_net_dbg.txt", "a" );
    if( f )
    {
        std::lock_guard< std::mutex > lock( m );
        va_list args;
        va_start( args, fmt );
        vfprintf( f, fmt, args );
        va_end( args );
        fprintf( f, "\n" );
        fflush( f );
    }
#endif
}

} // namespace atui::net
} // namespace atui
