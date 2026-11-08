// Implementation of the app_wait tool. See app_wait_tool.h.
//
// A whole tool for doing nothing, and the reason is physical: acting
// and looking need a breath between them. A launched program takes
// seconds to build its windows; a screenshot taken at once photographs
// an empty desk, and a model that believes the desk is empty starts
// inventing windows. The clamp is for the same protective reason --
// the chat client owns the clock, not the model.
#include "tools/app_wait_tool.h"

#include "core/strutil.h"
#include "platform/appman.h"
#include "tools/json_args.h"

namespace atui {
namespace tools {

const char *AppWaitTool::name() const
{
    return "app_wait";
}

const char *AppWaitTool::description() const
{
    return "Pause for a moment (milliseconds, capped at 15000) before "
           "the next tool call. Use it after app_launch and before "
           "looking: windows take a second or two to appear, and "
           "waiting is cheaper than re-listing an empty desktop three "
           "times and concluding the program crashed.";
}

const char *AppWaitTool::parametersSchema() const
{
    return "{\"type\":\"object\",\"properties\":{"
           "\"ms\":{\"type\":\"string\",\"description\":"
           "\"milliseconds to wait, decimal\"}"
           "},\"required\":[\"ms\"]}";
}

std::string AppWaitTool::execute( const std::string &argumentsJson )
{
    if( !looksJsonObject( argumentsJson ) )
        return "app_wait: arguments must be a JSON object";

    std::string word;
    if( !jsonStringArg( argumentsJson, "ms", word ) )
        return "app_wait: \"ms\" is required";

    unsigned long ms = 0;
    if( !atui::platform::parseMessageWord( word, ms ) )
        return "app_wait: \"ms\" is not a number";

    const unsigned long kCap = 15000UL;
    bool clamped = ms > kCap;
    if( clamped )
        ms = kCap;

    atui::platform::appSleepMs( (int)ms );
    std::string out = "app_wait: waited " + atui::core::toString( (long)ms )
                      + " ms";
    if( clamped )
        out += " (capped)";
    return out;
}

std::string AppWaitTool::approvalSummary( const std::string &argumentsJson ) const
{
    std::string word;
    jsonStringArg( argumentsJson, "ms", word );
    return std::string( "wait " ) + word + " ms";
}

std::string AppWaitTool::callPreview( const std::string &argumentsJson ) const
{
    return approvalSummary( argumentsJson );
}

AppWaitTool g_appWaitTool;

} // namespace atui::tools
} // namespace atui
