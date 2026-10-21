#include "dbg.h"

// Guarded, and not at file scope in the usual way: `<mutex>` is a C++11 header that
// Open Watcom does not ship, so a debug-off build -- which is the shipping build, and
// the one the Watcom target compiles -- must not look at it at all.  A debug build is
// a host-build thing, and on those hosts `<mutex>` exists.
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
