// windows: the desktop's top-level windows, as text.
//
// The reading half of the desktop pair: handles, geometry, titles and
// classes -- what a model needs to answer "is it open", "where is it",
// "which one did they mean" without pixels. Read-only, so unapproved by
// the same rule that lets get_time run: it changes nothing, and titles
// were already on the screen the user is looking at.
//
// C++98.
#ifndef ATUI_TOOLS_WINDOWS_TOOL_H
#define ATUI_TOOLS_WINDOWS_TOOL_H

#include "tools/tool.h"

namespace atui {
namespace tools {

class WindowsTool : public Tool {
public:
    const char *name() const;
    const char *description() const;
    const char *parametersSchema() const;
    std::string execute( const std::string &argumentsJson );
};

// Shared instance used by registerDefaults().
extern WindowsTool g_windowsTool;

} // namespace atui::tools
} // namespace atui

#endif // ATUI_TOOLS_WINDOWS_TOOL_H
