// app_wait: breathe between acts. See app_wait_tool.cpp.
//
// C++98.
#ifndef ATUI_TOOLS_APP_WAIT_TOOL_H
#define ATUI_TOOLS_APP_WAIT_TOOL_H

#include "tools/tool.h"

namespace atui {
namespace tools {

class AppWaitTool : public Tool {
public:
    const char *name() const;
    const char *description() const;
    const char *parametersSchema() const;
    std::string execute( const std::string &argumentsJson );
    bool requiresApproval() const { return true; }
    std::string approvalSummary( const std::string &argumentsJson ) const;
    std::string callPreview( const std::string &argumentsJson ) const;
};

extern AppWaitTool g_appWaitTool;

} // namespace atui::tools
} // namespace atui

#endif // ATUI_TOOLS_APP_WAIT_TOOL_H
