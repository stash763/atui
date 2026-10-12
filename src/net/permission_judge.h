// The permission agent, backed by the configured provider.
//
// A small model reads one tool call and describes what it would do;
// tools/permissions.cpp decides. See tools/judge.h for why the answer is a list
// of effects rather than a verdict, and tools/permissions.h for what is done
// with it.
//
// Network-only by nature: with no transport there is nobody to ask, and the
// policy then falls back to asking the user - the correct answer in a build that
// cannot reach a model.
#ifndef ATUI_NET_PERMISSION_JUDGE_H
#define ATUI_NET_PERMISSION_JUDGE_H

#include <string>

#include "tools/judge.h"

namespace atui {
namespace net {

// Which model the agent asks: [permissions] judge_model when set, otherwise the
// active provider's model. The name goes into cache keys and log lines, so the
// request and the record of it must both come from this one function.
std::string permissionAgentModel();

// Reads the agent's answer into `plan`. Line-oriented and tolerant, because that
// is the format a small model reliably produces:
//
//   READ <path>        WRITE <path>       NET <yes|no>
//   EXEC <yes|no>      ELEVATED <yes|no>  SENSITIVE <yes|no>
//   SUMMARY <text>
//
// Bullets, colons after the key, and stray backticks are forgiven; anything
// else is ignored. A flag whose value cannot be read counts as set - the model
// mentioned networking or privileges and said something unreadable about them,
// and "ask the human" is the honest outcome. `plan.parsed` becomes true only if
// at least one key was recognised, so an apology or a poem fails closed.
void parseEffectPlan( const std::string &reply, atui::tools::EffectPlan &plan );

// The OpenAI-compatible judge: one short, cheap request per call the facts could
// not settle, with a wall-clock budget of its own (Options > Settings).
class OaiPermissionJudge : public atui::tools::PermissionJudge {
public:
    void plan( const std::string &toolName,
               const std::string &argumentsJson,
               atui::tools::EffectPlan &out ) override;
};

// The instance installed at startup (the TUI owns its lifetime, as with the
// approver).
OaiPermissionJudge &oaiPermissionJudge();

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_PERMISSION_JUDGE_H
