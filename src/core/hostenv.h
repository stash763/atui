// Where this program is actually running -- stated once, up front.
//
// A model that is told nothing about its host believes its training
// data, and its training data says bash. The first tool call on an
// OS/2 box reaches for `ls` and `/dev/null`; the fix is not a tool
// that translates, it is a sentence the model reads before it writes
// its first command. This module is that sentence: compiled facts
// about the host, phrased for the system message of every request.
//
// Everything here is compile-time: the binary was built for one host,
// it knows which, and asking the running machine would only add ways
// to be wrong. Keep the brief short -- it rides every request -- and
// keep it true: it describes the shell run_command actually hands the
// command line to, and the path syntax the file tools actually accept.
//
// C++98, standard library only.
#ifndef ATUI_CORE_HOSTENV_H
#define ATUI_CORE_HOSTENV_H

#include <string>

namespace atui {
namespace core {

// The host's name as the model should hear it: "OS/2", "Linux",
// "macOS", "Windows", or "an unknown".
const char *hostOsName();

// Whether the host speaks POSIX (the run_command shell is /bin/sh and
// the usual Unix idioms hold).
bool hostIsPosix();

// The paragraph for the system message: host name, what the command
// shell really is, which idioms exist and which kill the turn, and
// the path syntax. Every request; a few dozen tokens buying back the
// hundreds a misfired shell habit costs.
std::string hostEnvBrief();

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_HOSTENV_H
