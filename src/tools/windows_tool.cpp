// Implementation of the windows tool. See windows_tool.h.
#include "tools/windows_tool.h"

#include "platform/screen.h"
#include "core/strutil.h"
#include "tools/json_args.h"

namespace atui {
namespace tools {

const char *WindowsTool::name() const
{
    return "windows";
}

const char *WindowsTool::description() const
{
    return "List the desktop's top-level windows: handle, screen "
           "position and size, title, resource class; ACTIVE marks the "
           "focused window and iconic the minimized ones. Use this to "
           "find a window to capture with the screenshot tool, or to "
           "confirm an application opened. Pass children=true to also "
           "list each window's child windows one level deep. Handles "
           "round-trip into the screenshot tool's window argument.";
}

const char *WindowsTool::parametersSchema() const
{
    return "{\"type\":\"object\",\"properties\":{"
           "\"children\":{\"type\":\"boolean\",\"description\":"
           "\"include each window's child windows (one level)\"}"
           "},\"required\":[]}";
}

std::string WindowsTool::execute( const std::string &argumentsJson )
{
    if( !looksJsonObject( argumentsJson ) )
        return "windows: arguments must be a JSON object";

    if( !atui::platform::windowsAvailable() )
        return "windows: window listing is not available on this platform";

    bool children = false;
    jsonBoolArg( argumentsJson, "children", children );

    std::vector< atui::platform::WindowInfo > list;
    if( !atui::platform::listWindows( list, children ) )
        return "windows: could not read the window list";

    if( list.empty() )
        return "windows: no windows found";

    std::string out;
    out += atui::core::toString( (long)list.size() ) + " top-level window(s):\n";
    out += atui::platform::formatWindows( list );
    return out;
}

WindowsTool g_windowsTool;

} // namespace atui::tools
} // namespace atui
