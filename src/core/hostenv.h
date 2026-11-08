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
#include <vector>

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

// --- the agent's prompts: what rides every request as system --------
//
// The system message is the brief above plus whatever the user adds,
// and this is the state that decides which of the two says what. It
// is a struct of plain fields because it is read on every request,
// edited by a dialog, and written to the config file -- three hands,
// one shape, no behavior to keep in step.
//
// The lines of `extra` are joined with newlines and appended after
// the brief. They ride as written: the model is told these are the
// user's instructions, so a line that says "always answer in German"
// arrives as exactly that authority, and nothing here interprets them.

struct AgentPrompts
{
    // Include the auto-generated host brief (hostEnvBrief()). The
    // default is true because a model that does not know its OS
    // reaches for the wrong commands; switching it off is telling
    // the machine it is somewhere else, which is a thing users do
    // deliberately, to point a friendly model at a persona.
    bool hostBrief;

    // The user's instruction lines, in order, empty ones dropped at
    // assembly. Capped at kMaxExtraLines when saved; a config file
    // with more is read up to the cap and left as it is.
    std::vector<std::string> extra;

    AgentPrompts() : hostBrief( true ) {}
};

// How many lines the whole `extra` block may hold: the number that
// fits an editor on every front atui draws dialogs on, so the dialog
// is a complete editor rather than a window onto half the file.
const std::size_t kMaxExtraLines = 6;

// The one instance: seeded from the config file by settings::loadAll,
// edited by Options > Prompts, written back by settings::saveAll.
AgentPrompts &agentPrompts();

// Assemble the system message from the settings above: the brief if
// wanted, then the extra lines (joined), each part present or absent
// on its own. Empty when both are off -- and empty means the request
// carries no system message, which is the honest answer to "say
// nothing about the environment and say nothing extra".
std::string buildSystemMessage();

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_HOSTENV_H
