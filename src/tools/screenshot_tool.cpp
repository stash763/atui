// Implementation of the screenshot tool. See screenshot_tool.h.
#include "tools/screenshot_tool.h"

#include "platform/screen.h"
#include "core/strutil.h"
#include "tools/json_args.h"

#include <vector>

namespace atui {
namespace tools {

namespace {

// The one-line shape both the approval prompt and the transcript want.
std::string describe( const std::string &target, const std::string &window,
                      const std::string &path )
{
    std::string out( "screenshot: " );
    out += target;
    if( target == "window" )
    {
        out += ' ';
        if( window.empty() )
            out += "(no window given)";
        else
            out += "'" + window + "'";
    }
    out += " -> ";
    out += path.empty() ? "the next atui_shot_N.bmp" : path;
    return out;
}

} // anonymous namespace

const char *ScreenshotTool::name() const
{
    return "screenshot";
}

const char *ScreenshotTool::description() const
{
    return "Capture a screenshot and save it as a BMP file. "
           "target=\"screen\" (the default) takes the whole screen; "
           "target=\"window\" with window=<handle or title text> takes "
           "that window's own contents, whole even where another window "
           "covers it (use the windows tool to find handles). Optional "
           "path names the output file relative to the working "
           "directory; without it the shot is saved as atui_shot_N.bmp. "
           "The result is the saved path and size: this text-only chat "
           "cannot show pictures, so open the file to look.";
}

const char *ScreenshotTool::parametersSchema() const
{
    return "{\"type\":\"object\",\"properties\":{"
           "\"target\":{\"type\":\"string\",\"enum\":[\"screen\",\"window\"],"
           "\"description\":\"what to capture (default screen)\"},"
           "\"window\":{\"type\":\"string\",\"description\":"
           "\"window handle or title text, for target=window\"},"
           "\"path\":{\"type\":\"string\",\"description\":"
           "\"output file; .bmp appended when missing\"}"
           "},\"required\":[]}";
}

std::string ScreenshotTool::execute( const std::string &argumentsJson )
{
    if( !looksJsonObject( argumentsJson ) )
        return "screenshot: arguments must be a JSON object";

    if( !atui::platform::captureAvailable() )
        return "screenshot: screen capture is not available on this platform";

    std::string target, window, path;
    jsonStringArg( argumentsJson, "target", target );
    jsonStringArg( argumentsJson, "window", window );
    jsonStringArg( argumentsJson, "path", path );
    if( target.empty() )
        target = "screen";
    path = atui::platform::ensureImagePath( path );
    if( path.empty() )
    {
        path = atui::platform::defaultShotPath();
        if( path.empty() )
            return "screenshot: no free atui_shot_N.bmp name in the working "
                   "directory, and none was given";
    }

    atui::platform::CaptureResult res;
    std::string subject;

    if( target == "screen" )
    {
        subject = "screen";
        res = atui::platform::captureScreen( path );
    }
    else if( target == "window" )
    {
        if( window.empty() )
            return "screenshot: target=window needs window=<handle or "
                   "title text> (the windows tool lists them)";

        std::vector< atui::platform::WindowInfo > list;
        if( !atui::platform::listWindows( list, false ) )
            return "screenshot: could not read the window list to find \'"
                   + window + "\'";

        std::vector< const atui::platform::WindowInfo * > matches;
        atui::platform::findWindows( list, window, matches );

        if( matches.empty() )
            return "screenshot: no window matches \'" + window
                 + "\' (the windows tool lists what is open)";
        if( matches.size() > 1 )
        {
            std::string out = "screenshot: \'" + window + "\' matches "
                + atui::core::toString( (long)matches.size() )
                + " windows; pick one by handle:\n";
            for( size_t i = 0; i < matches.size(); ++i )
                out += matches[ i ]->handle + "  \"" + matches[ i ]->title
                     + "\"\n";
            return out;
        }

        subject = "window " + matches[ 0 ]->handle;
        res = atui::platform::captureWindow( matches[ 0 ]->handle, path );
    }
    else
    {
        return "screenshot: target must be \"screen\" or \"window\"";
    }

    if( !res.ok )
        return "screenshot: " + res.error;

    return "saved " + res.path + " ("
         + atui::core::toString( (long)res.width ) + "x"
         + atui::core::toString( (long)res.height ) + " BMP)";
}

bool ScreenshotTool::requiresApproval() const
{
    return true;
}

PermissionSubject ScreenshotTool::permissionSubject(
    const std::string & ) const
{
    // The picture is the point and the picture is the whole screen: a
    // path subject would reason about the wrong half of the call.
    return PermissionSubject();
}

std::string ScreenshotTool::approvalSummary(
    const std::string &argumentsJson ) const
{
    std::string target, window, path;
    jsonStringArg( argumentsJson, "target", target );
    jsonStringArg( argumentsJson, "window", window );
    jsonStringArg( argumentsJson, "path", path );
    if( target.empty() )
        target = "screen";
    path = atui::platform::ensureImagePath( path );
    return describe( target, window, path );
}

std::string ScreenshotTool::callPreview(
    const std::string &argumentsJson ) const
{
    return approvalSummary( argumentsJson );
}

ScreenshotTool g_screenshotTool;

} // namespace atui::tools
} // namespace atui
