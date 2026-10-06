// Registry of tools available to the model. A process-wide set, rendered into
// the OpenAI "tools" array and consulted to execute calls by name.
//
// C++14.
#ifndef ATUI_TOOLS_TOOL_REGISTRY_H
#define ATUI_TOOLS_TOOL_REGISTRY_H

#include "tools/tool.h"

#include <string>
#include <vector>

namespace atui {
namespace tools {

class ToolRegistry {
public:
    static ToolRegistry &instance();

    // Register a tool (non-owning; the caller keeps it alive, typically a
    // static instance). Later registrations with the same name win.
    void add( Tool *tool );

    // Look up by name; returns 0 if not found.
    Tool *find( const std::string &name ) const;

    const std::vector<Tool *> &all() const { return tools_; }

    // Render the OpenAI "tools" array JSON, or "" if no tools are registered.
    std::string renderToolsJson() const;

private:
    std::vector<Tool *> tools_;
};

// Register the built-in tools (idempotent). Called before the first request.
void registerDefaults();

} // namespace atui::tools
} // namespace atui

#endif // ATUI_TOOLS_TOOL_REGISTRY_H
