// Implementation of the run_command tool. See run_command_tool.h.
#include "tools/run_command_tool.h"
#include "tools/json_args.h"
#include "tools/preview.h"

#include <cstdio>
#include <sstream>
#include <string>

#if !defined(_WIN32)
#include <sys/wait.h>
#define ATUI_POPEN( cmd, mode )  popen( ( cmd ), ( mode ) )
#define ATUI_PCLOSE( p )         pclose( ( p ) )
#else
#define ATUI_POPEN( cmd, mode )  _popen( ( cmd ), ( mode ) )
#define ATUI_PCLOSE( p )         _pclose( ( p ) )
#endif

namespace atui {
namespace tools {

namespace {

// Bound captured output so a chatty command cannot blow up memory or the
// transcript. Everything past this is discarded (the child is still drained).
const std::size_t kMaxOutput = 200 * 1024;

} // anonymous namespace

const char *RunCommandTool::name() const
{
    return "run_command";
}

const char *RunCommandTool::description() const
{
    return "Run a shell command on the user's machine and return its combined "
           "stdout/stderr output and exit code. Use for builds, tests, git, "
           "listing or printing files, etc. The command runs in the app's "
           "working directory through /bin/sh and with the user's own "
           "permissions. Very long output is truncated.";
}

const char *RunCommandTool::parametersSchema() const
{
    return "{\"type\":\"object\",\"properties\":{"
           "\"command\":{\"type\":\"string\",\"description\":\"Shell command "
           "to execute (POSIX sh syntax)\"}"
           "},\"required\":[\"command\"]}";
}

std::string RunCommandTool::execute( const std::string &argumentsJson )
{
    if( !looksJsonObject( argumentsJson ) )
        return "run_command: arguments must be a JSON object";

    std::string command;
    jsonStringArg( argumentsJson, "command", command );
    if( command.empty() )
        return "run_command: 'command' is required";

    // Merge stderr into stdout so the model sees errors too. popen() runs the
    // string through the shell. On POSIX we redirect the shell's stderr onto
    // its stdout (the pipe) up front so even commands that themselves do
    // `1>&2` are captured; cmd.exe has no `exec`, so append `2>&1` there.
#if !defined(_WIN32)
    std::string shellCmd = "exec 2>&1; " + command;
#else
    std::string shellCmd = command + " 2>&1";
#endif
    FILE *pipe = ATUI_POPEN( shellCmd.c_str(), "r" );
    if( !pipe )
        return "run_command: failed to start '" + oneLineClip( command, 80 ) + "'";

    std::string out;
    bool truncated = false;
    char buf[4096];
    std::size_t n;
    while( ( n = std::fread( buf, 1, sizeof buf, pipe ) ) > 0 )
    {
        if( out.size() < kMaxOutput )
        {
            std::size_t room = kMaxOutput - out.size();
            if( n <= room ) out.append( buf, n );
            else { out.append( buf, room ); truncated = true; }
        }
        // else: keep draining so the child is never blocked on a full pipe.
    }

    int status = ATUI_PCLOSE( pipe );

    std::ostringstream r;
    int code = -1;
    bool signalled = false;
    int sig = 0;
#if !defined(_WIN32)
    if( status != -1 && WIFSIGNALED( status ) )
    { signalled = true; sig = WTERMSIG( status ); }
    else if( status != -1 )
        code = WEXITSTATUS( status );
#else
    code = status;
#endif

    if( signalled )
        r << "terminated by signal " << sig;
    else
        r << "exit code " << code;

    if( out.empty() )
    {
        r << " (no output)";
    }
    else
    {
        // Trim a single trailing newline for readability.
        if( out[out.size() - 1] == '\n' ) out.erase( out.size() - 1 );
        r << "\n\n" << out;
        if( truncated )
            r << "\n\n[output truncated at " << kMaxOutput << " bytes]";
    }
    return r.str();
}

PermissionSubject RunCommandTool::permissionSubject(
    const std::string &argumentsJson ) const
{
    std::string command;
    jsonStringArg( argumentsJson, "command", command );
    // The model's own text, not the `exec 2>&1;` wrapper execute() adds: the
    // policy is judging what was asked for.
    return PermissionSubject( psCommand, command );
}

std::string RunCommandTool::approvalSummary( const std::string &argumentsJson ) const
{
    std::string command;
    jsonStringArg( argumentsJson, "command", command );
    if( command.empty() )
        return "Run a shell command (no command given)";
    return "Run command:\n  $ " + oneLineClip( command, 200 );
}

std::string RunCommandTool::callPreview( const std::string &argumentsJson ) const
{
    std::string command;
    jsonStringArg( argumentsJson, "command", command );
    if( command.empty() )
        return "run_command  (no command given)";
    // The transcript shows this after a "$ " marker, one shell command line per
    // line of the preview.
    return previewClip( command, kMaxPreview );
}

RunCommandTool g_runCommandTool;

} // namespace atui::tools
} // namespace atui
