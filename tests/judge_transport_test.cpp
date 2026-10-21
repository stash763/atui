// Tests for the permission agent's own network call: the question it asks, and what
// comes back as a plan.
//
// The policy and the plan cache are tested rung by rung in `selftest.cpp` against a
// fake agent; what is tested here is the other half -- that `OaiPermissionJudge` puts
// the right question on the wire and turns an answer into a plan, including the two
// ways it can fail to answer at all.  Without these, a judge that never worked would
// look like a policy that asks a lot of questions.
//
// Network-only: nothing here is compiled when ATUI_HAVE_NETWORK is off.
#include "harness.h"

#ifdef ATUI_HAVE_NETWORK

#include "http_pipe.h"
#include "sse_script.h"

#include "net/permission_judge.h"
#include "net/provider.h"

#include "tools/permissions.h"

#include <string>

namespace atui_test {

namespace {

using atui::net::OaiPermissionJudge;
using atui::tools::EffectPlan;

// Saves what the judge reads (the endpoint, the agent model, the budget) and puts it
// back afterwards: like everything else in this program the settings are
// process-wide, and a judge pointed at a dead port left behind by one test would
// make the next one's failure meaningless.
class JudgeFixture
{
public:
    bool ok;

    JudgeFixture()
        : ok( false )
        , savedProvider_( atui::net::providerSettings() )
        , savedOverride_( atui::tools::judgeModelOverride() )
        , savedSeconds_( atui::tools::judgeSeconds() )
    {
        atui::tools::clearPlanCache();
        ok = true;
    }

    ~JudgeFixture()
    {
        atui::net::providerSettings() = savedProvider_;
        atui::tools::judgeModelOverride() = savedOverride_;
        atui::tools::judgeSeconds() = savedSeconds_;
        atui::tools::clearPlanCache();
    }

    void pointAt( int port )
    {
        atui::net::ProviderSettings &p = atui::net::providerSettings();
        p.baseUrl = "http://127.0.0.1:" + std::to_string( port ) + "/v1";
        p.apiKey = "";
        p.model = "chat-model";
        atui::tools::judgeModelOverride() = "agent-model";
        atui::tools::judgeSeconds() = 5;
    }

private:
    JudgeFixture( const JudgeFixture & );
    JudgeFixture &operator=( const JudgeFixture & );

    atui::net::ProviderSettings savedProvider_;
    std::string savedOverride_;
    int savedSeconds_;
};

// The plan for one call, asked of the real judge over the scripted server.
EffectPlan ask( OaiPermissionJudge &judge, const char *tool,
                const std::string &arguments )
{
    EffectPlan plan;
    judge.plan( tool, arguments, plan );
    return plan;
}

int test_judge_asks_about_the_call_it_was_given()
{
    JudgeFixture f;
    if( !f.ok ) return -1;
    ScriptedHttp server;
    if( !server.start() ) return -2;
    f.pointAt( server.port() );
    server.enqueueResponse( sseResponse( sseAnswer( "SUMMARY writes a log\n"
                                                    "WRITE build.log\n" ) ) );

    OaiPermissionJudge judge;
    const EffectPlan plan = ask( judge, "run_command",
                                 "{\"command\":\"make >> build.log\"}" );

    const std::string wire = server.lastRequest();
    if( wire.find( "POST /v1/chat/completions HTTP/1.1\r\n" ) != 0 ) return -3;
    if( wire.find( "Describe this call." ) == std::string::npos ) return -4;
    // The agent has to be told which call it is describing, and which tree: the
    // same command means different things in different places.
    if( wire.find( "run_command" ) == std::string::npos ) return -5;
    if( wire.find( "make >> build.log" ) == std::string::npos ) return -6;
    if( wire.find( "\\\"command\\\"" ) == std::string::npos ) return -7;
    // Judged by the agent model, not the chat model: which model is asked is a
    // setting somebody changes, and it is only visible in the body.
    if( wire.find( "\"model\":\"agent-model\"" ) == std::string::npos ) return -8;

    if( !plan.parsed ) return -9;
    if( plan.summary != "writes a log" ) return -10;
    if( plan.writes.size() != 1 || plan.writes[0] != "build.log" ) return -11;
    return 0;
}

// A model that thinks out loud and runs out of budget before answering has still
// described the call, and the description is in the other field.
int test_judge_reads_the_reasoning_when_there_is_nothing_else()
{
    JudgeFixture f;
    if( !f.ok ) return -1;
    ScriptedHttp server;
    if( !server.start() ) return -2;
    f.pointAt( server.port() );
    server.enqueueResponse(
        sseResponse( deltaJson( "reasoning_content",
                                "It compiles the project.\nNET no\n" ) +
                     "data: [DONE]\n\n" ) );

    OaiPermissionJudge judge;
    const EffectPlan plan = ask( judge, "run_command",
                                 "{\"command\":\"make -C . test\"}" );
    if( !plan.parsed ) return -3;
    return 0;
}

// Every way the agent can fail to answer leaves the plan un-parsed, which the policy
// reads as "undescribed" and never runs.  A 500 is the common one.
int test_judge_a_refusal_describes_nothing()
{
    JudgeFixture f;
    if( !f.ok ) return -1;
    ScriptedHttp server;
    if( !server.start() ) return -2;
    f.pointAt( server.port() );
    server.enqueueResponse(
        errorResponse( "500 Internal Server Error", "{\"error\":\"no gpu\"}" ) );

    OaiPermissionJudge judge;
    const EffectPlan plan = ask( judge, "run_command", "anything" );
    if( plan.parsed ) return -3;
    if( !plan.writes.empty() || plan.net || plan.exec ) return -4;
    return 0;
}

// Prose is not a plan.  This is the same rule the offline parser enforces; here it is
// checked end to end, because the shape the model actually replies in -- markdown,
// a code fence, a sentence -- is what tests this.
int test_judge_prose_describes_nothing()
{
    JudgeFixture f;
    if( !f.ok ) return -1;
    ScriptedHttp server;
    if( !server.start() ) return -2;
    f.pointAt( server.port() );
    server.enqueueResponse(
        sseResponse( sseAnswer( "```\nThe command builds the project and writes "
                                "nothing important.\n```\n" ) ) );

    OaiPermissionJudge judge;
    const EffectPlan plan = ask( judge, "run_command", "anything" );
    // A fenced block with recognisable lines inside it still counts (the decorations
    // are stripped) -- which is why the assertion is about the *paths* rather than
    // about `parsed`: a sentence has no paths in it, and that is what an undescribed
    // call must not have.
    if( !plan.writes.empty() ) return -5;
    if( plan.net || plan.exec || plan.elevated || plan.sensitive ) return -6;
    return 0;
}

// With no agent model to name there is nobody to ask, and `plan()` must not open a
// connection: the caller falls back to asking the user, and a request to the chat
// model in its place would be a permission decision made by the wrong model.
int test_judge_asks_nothing_without_an_agent_model()
{
    JudgeFixture f;
    if( !f.ok ) return -1;
    ScriptedHttp server;
    if( !server.start() ) return -2;
    f.pointAt( server.port() );
    atui::tools::judgeModelOverride() = "";
    atui::net::providerSettings().model = "";
    server.enqueueResponse( sseResponse( sseAnswer( "WRITE secret" ) ) );

    OaiPermissionJudge judge;
    const EffectPlan plan = ask( judge, "edit_file", "anything" );
    if( plan.parsed ) return -3;
    if( !server.lastRequest().empty() ) return -4;      // nothing went out
    return 0;
}

} // anonymous namespace

void init_permission_judge_tests()
{
    register_test( "judge: asks about the call it was given",
                   &test_judge_asks_about_the_call_it_was_given );
    register_test( "judge: reads the reasoning when there is nothing else",
                   &test_judge_reads_the_reasoning_when_there_is_nothing_else );
    register_test( "judge: a refusal describes nothing",
                   &test_judge_a_refusal_describes_nothing );
    register_test( "judge: prose describes nothing",
                   &test_judge_prose_describes_nothing );
    register_test( "judge: asks nothing without an agent model",
                   &test_judge_asks_nothing_without_an_agent_model );
}

} // namespace atui_test

#endif // ATUI_HAVE_NETWORK
