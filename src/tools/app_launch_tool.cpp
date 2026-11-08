// Implementation of the app_launch tool. See app_launch_tool.h.
//
// The existence check before the session call is not fussiness: on a
// machine where paths are case-temperamental, "program not found" is
// a better first answer than a session error code with a number in it.
#include "tools/app_launch_tool.h"

#include "core/strutil.h"
#include "platform/appman.h"
#include "tools/json_args.h"

#include <cstdio>

namespace atui {
namespace tools {

const char *AppLaunchTool::name() const
{
    return "app_launch";
}

const char *AppLaunchTool::description() const
{
    return "Start another program by full path (e.g. \"C:\\\\OS2\\\\E.EXE\"). "
           "The answer says whether it started in the foreground or the "
           "background and its pid when the machine could learn one: a "
           "background start from a session that does not own the display "
           "runs without ever showing a window, so treat that answer as a "
           "warning, not a style note. Program arguments are not passed "
           "yet. Check the path exists first (read_file or dir_list); "
           "the tool checks too, but its file-not-found is a last resort.";
}

const char *AppLaunchTool::parametersSchema() const
{
    return "{\"type\":\"object\",\"properties\":{"
           "\"program\":{\"type\":\"string\",\"description\":"
           "\"absolute path of the program to start\"}"
           "},\"required\":[\"program\"]}";
}

std::string AppLaunchTool::execute( const std::string &argumentsJson )
{
    if( !looksJsonObject( argumentsJson ) )
        return "app_launch: arguments must be a JSON object";

    if( !atui::platform::appLaunchAvailable() )
        return "app_launch: starting programs is not available on this "
               "platform";

    std::string program;
    if( !jsonStringArg( argumentsJson, "program", program )
        || program.empty() )
        return "app_launch: \"program\" (absolute path) is required";

    std::FILE *probe = std::fopen( program.c_str(), "rb" );
    if( probe == 0 )
        return "app_launch: no readable program at \"" + program + "\"";
    std::fclose( probe );

    std::string how, error;
    unsigned long pid = 0;
    if( !atui::platform::appLaunch( program, how, pid, error ) )
        return "app_launch: " + error;

    std::string out = "app_launch: started \"" + program + "\" (" + how;
    if( pid != 0 )
        out += ", pid " + atui::core::toString( (long)pid );
    out += ")";
    return out;
}

PermissionSubject AppLaunchTool::permissionSubject(
    const std::string &argumentsJson ) const
{
    std::string program;
    jsonStringArg( argumentsJson, "program", program );
    // The always-allow gesture the shell trained: allow this program,
    // not every program.
    return PermissionSubject( psCommand, program );
}

std::string AppLaunchTool::approvalSummary( const std::string &argumentsJson ) const
{
    std::string program;
    jsonStringArg( argumentsJson, "program", program );
    return std::string( "launch " ) + program;
}

std::string AppLaunchTool::callPreview( const std::string &argumentsJson ) const
{
    return approvalSummary( argumentsJson );
}

AppLaunchTool g_appLaunchTool;

} // namespace atui::tools
} // namespace atui
