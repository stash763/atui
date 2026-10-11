// run_command: execute a shell command and return its output + exit code.
//
// The command is run through the system shell (`/bin/sh -c` on POSIX), with
// stderr merged into stdout, and the combined output is captured (capped, to
// bound memory/transcript size). Requires user approval before it runs.
//
// C++14.
#ifndef ATUI_TOOLS_RUN_COMMAND_TOOL_H
#define ATUI_TOOLS_RUN_COMMAND_TOOL_H

#include "tools/tool.h"

namespace atui {
namespace tools {

class RunCommandTool : public Tool {
public:
    const char *name() const override;
    const char *description() const override;
    const char *parametersSchema() const override;
    std::string execute( const std::string &argumentsJson ) override;
    bool requiresApproval() const override { return true; }
    // The command line itself: containment decides the paths it names, and the
    // permission agent describes the rest.
    PermissionSubject permissionSubject(
        const std::string &argumentsJson ) const override;
    std::string approvalSummary( const std::string &argumentsJson ) const override;
    // The transcript record is the command itself, so it reads like a shell
    // history line ("$ make test") and multi-line commands stay readable.
    std::string callPreview( const std::string &argumentsJson ) const override;
};

// Shared instance used by registerDefaults().
extern RunCommandTool g_runCommandTool;

} // namespace atui::tools
} // namespace atui

#endif // ATUI_TOOLS_RUN_COMMAND_TOOL_H
