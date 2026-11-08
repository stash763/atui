// app_launch: start another program. See app_launch_tool.cpp.
//
// C++98.
#ifndef ATUI_TOOLS_APP_LAUNCH_TOOL_H
#define ATUI_TOOLS_APP_LAUNCH_TOOL_H

#include "tools/tool.h"

namespace atui {
namespace tools {

class AppLaunchTool : public Tool {
public:
    const char *name() const;
    const char *description() const;
    const char *parametersSchema() const;
    std::string execute( const std::string &argumentsJson );
    bool requiresApproval() const { return true; }
    PermissionSubject permissionSubject(
        const std::string &argumentsJson ) const;
    std::string approvalSummary( const std::string &argumentsJson ) const;
    std::string callPreview( const std::string &argumentsJson ) const;
};

extern AppLaunchTool g_appLaunchTool;

} // namespace atui::tools
} // namespace atui

#endif // ATUI_TOOLS_APP_LAUNCH_TOOL_H
