// Implementation of the get_time tool. See time_tool.h.
#include "tools/time_tool.h"

#include <cstdio>
#include <ctime>

// localtime_r is the re-entrant form; the tool runs on the request worker
// thread, so prefer it where available.
#if !defined(_WIN32)
#  define ATUI_LOCALTIME( t, out ) localtime_r( ( t ), ( out ) )
#else
#  define ATUI_LOCALTIME( t, out ) ( *( out ) = *localtime( ( t ) ) )
#endif

namespace atui {
namespace tools {

const char *TimeTool::name() const
{
    return "get_time";
}

const char *TimeTool::description() const
{
    return "Get the current local date and time from the user's machine. "
           "Use this for any question about the present time, date, day of "
           "the week, or clock. It takes no arguments.";
}

const char *TimeTool::parametersSchema() const
{
    return "{\"type\":\"object\",\"properties\":{},\"required\":[]}";
}

std::string TimeTool::execute( const std::string &argumentsJson )
{
    ( void ) argumentsJson; // takes no arguments

    std::time_t now = std::time( 0 );
    std::tm tmv;
    if( !ATUI_LOCALTIME( &now, &tmv ) )
        return "get_time: unable to read the current time";

    char buf[64];
    // ISO-style local timestamp; %Z may be empty on some platforms.
    std::size_t n = std::strftime( buf, sizeof buf, "%Y-%m-%d %H:%M:%S %Z", &tmv );
    if( n == 0 )
        return "get_time: unable to format the current time";

    return std::string( buf );
}

TimeTool g_timeTool;

} // namespace atui::tools
} // namespace atui
