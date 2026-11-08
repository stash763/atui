// Implementation of the tool registry. See tool_registry.h.
#include "tools/tool_registry.h"
#include "tools/time_tool.h"
#include "tools/edit_file_tool.h"
#include "tools/run_command_tool.h"
#include "tools/screenshot_tool.h"
#include "tools/windows_tool.h"
#include "tools/app_launch_tool.h"
#include "tools/app_window_tools.h"
#include "tools/app_wait_tool.h"
#include "platform/screen.h"
#include "platform/appman.h"

#include <mutex>

namespace atui {
namespace tools {

namespace {

// Minimal JSON string escaping (registry is network-independent, so it does
// not reuse the net-layer escape helper).
std::string esc( const std::string &s )
{
    std::string out;
    out.reserve( s.size() + 8 );
    for( std::size_t i = 0; i < s.size(); ++i )
    {
        switch( s[i] )
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += s[i];   break;
        }
    }
    return out;
}

} // anonymous namespace

ToolRegistry &ToolRegistry::instance()
{
    static ToolRegistry reg;
    return reg;
}

void ToolRegistry::add( Tool *tool )
{
    if( !tool ) return;
    const std::string name( tool->name() );
    // Replace any existing tool with the same name.
    for( std::size_t i = 0; i < tools_.size(); ++i )
    {
        if( std::string( tools_[i]->name() ) == name )
        {
            tools_[i] = tool;
            return;
        }
    }
    tools_.push_back( tool );
}

Tool *ToolRegistry::find( const std::string &name ) const
{
    for( std::size_t i = 0; i < tools_.size(); ++i )
        if( name == tools_[i]->name() )
            return tools_[i];
    return 0;
}

std::string ToolRegistry::renderToolsJson() const
{
    if( tools_.empty() ) return std::string();

    std::string s = "[";
    for( std::size_t i = 0; i < tools_.size(); ++i )
    {
        const Tool &t = *tools_[i];
        if( i ) s += ",";
        s += "{\"type\":\"function\",\"function\":{\"name\":\"";
        s += esc( t.name() );
        s += "\",\"description\":\"";
        s += esc( t.description() );
        s += "\",\"parameters\":";
        const char *schema = t.parametersSchema();
        if( schema && *schema ) s += schema;
        else                    s += "{\"type\":\"object\",\"properties\":{}}";
        s += "}}";
    }
    s += "]";
    return s;
}

namespace {

// A mutex at file scope rather than std::call_once, which Watcom does not have:
// an object of static storage duration is built before anything can call this
// function, so there is no second question about who initialises it first.
std::mutex g_registerMx;
bool g_registered = false;

} // anonymous namespace

void registerDefaults()
{
    std::lock_guard< std::mutex > lock( g_registerMx );
    if( g_registered ) return;
    g_registered = true;

    ToolRegistry &r = ToolRegistry::instance();
    r.add( &g_timeTool );
    r.add( &g_editFileTool );     // file-system write: gated by approval
    r.add( &g_runCommandTool );   // shell exec: gated by approval
    // The desktop pair, and only where a desktop answers: offering a
    // screenshot tool to a machine with no screen would hand the model a
    // way to fail. The question is asked once at startup because the
    // answer is a property of the session atui was started in.
    if( atui::platform::captureAvailable() )
        r.add( &g_screenshotTool );
    if( atui::platform::windowsAvailable() )
        r.add( &g_windowsTool );
    // The hands, gated the same way as the eyes and by harder
    // measurements: what is registered here is only what the pmdrive
    // probe did against live targets and survived. The list is short
    // on purpose -- notably there is no key-typing tool, because
    // message-level typing intermittently kills foreign apps (see the
    // TESTING.md journal), and a tool that must not be used is worse
    // than a missing one. app_wait belongs to the pair too: acting
    // without pausing to let the desktop settle only produces
    // screenshots of nothing.
    if( atui::platform::appLaunchAvailable() )
        r.add( &g_appLaunchTool );
    if( atui::platform::appPostAvailable() )
    {
        r.add( &g_appCloseTool );
        r.add( &g_appClickTool );
        r.add( &g_appSetTextTool );
        r.add( &g_appSendMsgTool );
        r.add( &g_appWaitTool );
    }
}

} // namespace atui::tools
} // namespace atui
