// Implementation of the tool registry. See tool_registry.h.
#include "tools/tool_registry.h"
#include "tools/time_tool.h"
#include "tools/edit_file_tool.h"
#include "tools/run_command_tool.h"

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
    for( char c : s )
    {
        switch( c )
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
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

void registerDefaults()
{
    static std::once_flag flag;
    std::call_once( flag, []() {
        ToolRegistry &r = ToolRegistry::instance();
        r.add( &g_timeTool );
        r.add( &g_editFileTool );     // file-system write: gated by approval
        r.add( &g_runCommandTool );   // shell exec: gated by approval
    } );
}

} // namespace atui::tools
} // namespace atui
