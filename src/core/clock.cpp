// See clock.h for what these guarantee and what they cannot.

#include "core/clock.h"

namespace atui {
namespace core {

// The re-entrant spelling is asked for only where it is known to exist: Watcom
// defines neither __unix__ nor _WIN32, and the platforms it stands for have the
// single-threaded form and nothing else.
#if defined( __unix__ ) || defined( __APPLE__ )
#define ATUI_GMTIME_R 1
#define ATUI_LOCALTIME_R 1
#else
#define ATUI_GMTIME_R 0
#define ATUI_LOCALTIME_R 0
#endif

bool utcBreakdown( std::time_t when, std::tm &out )
{
#if ATUI_GMTIME_R
    return 0 != ::gmtime_r( &when, &out );
#else
    const std::tm *p = std::gmtime( &when );
    if( 0 == p )
        return false;
    out = *p;
    return true;
#endif
}

bool localBreakdown( std::time_t when, std::tm &out )
{
#if ATUI_LOCALTIME_R
    return 0 != ::localtime_r( &when, &out );
#else
    const std::tm *p = std::localtime( &when );
    if( 0 == p )
        return false;
    out = *p;
    return true;
#endif
}

} // namespace atui::core
} // namespace atui
