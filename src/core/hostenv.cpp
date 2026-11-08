// Implementation of the host environment brief. See core/hostenv.h.
//
// The wording is deliberate: say what the shell is, name the commands
// that do not exist (models do not infer absence from a general
// statement nearly as well as from the list), and give one worked path
// so the backslash convention lands. The OS/2 paragraph is the long
// one because it is the far one: everything in the training data
// points the other way.
#include "core/hostenv.h"

namespace atui {
namespace core {

const char *hostOsName()
{
#if defined( __OS2__ )
    return "OS/2";
#elif defined( __APPLE__ )
    return "macOS";
#elif defined( __linux__ )
    return "Linux";
#elif defined( _WIN32 )
    return "Windows";
#else
    return "an unknown operating system";
#endif
}

bool hostIsPosix()
{
#if defined( __unix__ ) || defined( __APPLE__ )
    return true;
#else
    return false;
#endif
}

std::string hostEnvBrief()
{
    std::string s = std::string( "Environment: you are running on " )
                  + hostOsName() + ". ";
#if defined( __OS2__ )
    s +=
        "This is an IBM OS/2 family system (eComStation): it is NOT "
        "Unix, Linux, macOS or Windows, and Unix tools and habits do "
        "not apply. run_command hands the line to the OS/2 command "
        "processor (cmd.exe), not to sh: builtins are DIR, COPY, DEL "
        "(ERASE), TYPE, RENAME, MOVE, MD, RD, CD, CLS, ECHO, SET, "
        "START; pipes | and redirection > >> 2>&1 work, and stderr is "
        "already captured for you. There is no ls, cat, cp, mv, rm, "
        "grep, head, tail, sed, awk, touch, which, ps, pwd, chmod and "
        "no /dev/null (it is NUL); no sh or bash scripting, no $VAR "
        "or $(...) substitution (environment variables are %NAME%), "
        "no ~ home abbreviation, and single quotes are not quoting -- "
        "use double quotes. Paths are drive-lettered with backslashes "
        "(C:\\OS2\\E.EXE); slashes are not accepted by most programs, "
        "and a path without a drive lands on the current drive. Many "
        "utilities live in C:\\OS2\\BIN: DIR there before assuming a "
        "tool is missing. A command 'not recognized' almost always "
        "means a Unix habit; switch to the OS/2 name.";
#elif defined( __APPLE__ ) || defined( __unix__ )
    s +=
        "run_command hands the line to /bin/sh; POSIX idioms, paths "
        "and tools apply.";
#elif defined( _WIN32 )
    s +=
        "run_command hands the line to cmd.exe: DOS-style builtins "
        "(DIR, COPY, TYPE...), backslash paths, %NAME% variables; "
        "Unix tools are not assumed present.";
#else
    s += "The command shell is the system default command processor.";
#endif
    return s;
}

} // namespace atui::core
} // namespace atui
