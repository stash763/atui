// get_time: a tool that returns the machine's current local date and time.
//
// C++14.
#ifndef ATUI_TOOLS_TIME_TOOL_H
#define ATUI_TOOLS_TIME_TOOL_H

#include "tools/tool.h"

namespace atui {
namespace tools {

class TimeTool : public Tool {
public:
    const char *name() const override;
    const char *description() const override;
    const char *parametersSchema() const override;
    std::string execute( const std::string &argumentsJson ) override;
};

// Shared instance used by registerDefaults().
extern TimeTool g_timeTool;

} // namespace atui::tools
} // namespace atui

#endif // ATUI_TOOLS_TIME_TOOL_H
