// The permission agent: what a tool call is going to do.
//
// A small dedicated model describes the effects of a call; it never decides
// whether the call may run. That distinction is the whole design: an LLM is good
// at reading `make -C build test` and saying "compiles and runs tests, inside the
// project", and bad at being the security boundary. So the answer is a list of
// effects, and `permissions::decide()` applies the project containment policy to
// it. See permissions.h for the ladder.
//
// The agent is consulted only for calls that the facts alone cannot settle, it
// sees the call and the computed facts, never the conversation (anything that
// steered the main model must not travel to the gate), and every failure - no
// agent, timeout, an answer that cannot be read - lands on "ask".
//
// C++14. Network-independent: the interface and the cache have no HTTP in them;
// the OpenAI-backed implementation is in net/permission_judge.cpp.
#ifndef ATUI_TOOLS_JUDGE_H
#define ATUI_TOOLS_JUDGE_H

#include <string>
#include <vector>

namespace atui {
namespace tools {

// One description of what a call will do.
struct EffectPlan {
    // Paths the call reads from / writes to, as the agent understood them. They
    // are *claims*: containment is only trusted when this layer resolved the path
    // itself (see permissions::decide and the write rungs).
    std::vector< std::string > reads;
    std::vector< std::string > writes;

    bool net;        // reaches the network
    bool exec;       // executes downloaded or obfuscated code
    bool elevated;   // sudo/doas/privilege change
    bool sensitive;  // touches personal data (credentials, keychain, mail...)
    std::string summary;   // one line for the human reading the transcript

    // False when no usable description was obtained. Every decision rung treats
    // it as "ask", so an unparsable answer can never allow anything.
    bool parsed;

    EffectPlan()
        : net( false ), exec( false ), elevated( false ), sensitive( false ),
          parsed( false ) {}
};

// Implemented by the front end: net/OaiPermissionJudge when there is a model to
// ask, nothing at all in a non-network build (which is what makes the agent
// optional rather than assumed).
class PermissionJudge {
public:
    virtual ~PermissionJudge() {}

    // Describe what running `toolName( argumentsJson )` would do. Implementations
    // must not execute anything, must return promptly (the caller is waiting with
    // a budget), and must leave `out.parsed` false on any problem.
    virtual void plan( const std::string &toolName,
                       const std::string &argumentsJson,
                       EffectPlan &out ) = 0;
};

// Process-wide agent (not owned), installed at startup like the approver. Null
// means there is nobody to ask, and calls the facts cannot settle are asked
// about instead.
void setPermissionJudge( PermissionJudge *judge );
PermissionJudge *permissionJudge();

// --- verdict cache ------------------------------------------------------------
//
// A build/test loop runs the same command again and again, and paying a model
// round-trip every time is how a permission agent becomes the reason everything
// feels slow. Entries are keyed on everything the answer depends on - the tool,
// the exact arguments, the project root, the model, and the prompt version - so
// changing the prompt or the working directory cannot reuse a stale plan.

// Key for one call. `argumentsJson` is used verbatim: two different commands are
// two different questions. `model` and `root` are in the key because both change
// what the answer should be - the same command means different things in a
// different tree or under a different reader.
std::string planCacheKey( const std::string &toolName,
                          const std::string &argumentsJson,
                          const std::string &model,
                          const std::string &root );

// Cached plan for `key`, or false when there is none.
bool cachedPlan( const std::string &key, EffectPlan &out );

// Stores `plan`. Only plans that parsed are worth remembering.
void rememberPlan( const std::string &key, const EffectPlan &plan );

// The version of the question asked of the agent, for cache keys and logs.
const char *judgePromptVersion();

// Drops every entry (settings changed, tests, or the cap was reached).
void clearPlanCache();

} // namespace atui::tools
} // namespace atui

#endif // ATUI_TOOLS_JUDGE_H
