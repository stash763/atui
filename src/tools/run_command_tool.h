// run_command: execute a shell command and return its output + exit code.
//
// The command is run through the system shell (`/bin/sh -c` on POSIX), with stderr
// merged into stdout, and the combined output is captured (capped, to bound memory
// and transcript size). Requires user approval before it runs.
//
// Two things about the child are settings rather than accidents of `popen`: it does
// not inherit the credential-shaped environment variables (see core/childenv.h),
// and where the platform can stop a process it will not be allowed to run forever.
//
// C++14.
#ifndef ATUI_TOOLS_RUN_COMMAND_TOOL_H
#define ATUI_TOOLS_RUN_COMMAND_TOOL_H

#include <string>

#include "tools/tool.h"

namespace atui {
namespace tools {

// How long a command may run before atui stops it and everything it started.
// `[tools] command_timeout`, `ATUI_COMMAND_TIMEOUT`; 0 means no limit, which is a
// thing a person may want on a machine where a build legitimately takes an hour,
// and a thing they should have to type. A `long` rather than an `int` because on
// MS-DOS an `int` is 16 bits and cannot hold a limit longer than nine hours.
long &commandTimeoutSeconds();
long clampCommandTimeout( long seconds );

// Extra environment variable names to hand a command anyway, comma separated:
// `[tools] pass_env`, `ATUI_PASS_ENV`. For the build step that really does need
// `NPM_TOKEN` from the environment, as opposed to the ones that merely could.
std::string &passEnvNames();

// Whether this build can actually enforce the timeout (it needs fork(), a process
// group and poll()). Where it cannot, the timeout is reported in the result of a
// command that ran too long rather than enforced.
bool commandTimeoutIsEnforced();

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
