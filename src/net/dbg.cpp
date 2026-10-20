#include "dbg.h"
#include <mutex>

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
