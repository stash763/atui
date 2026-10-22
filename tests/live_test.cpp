// Opt-in integration tests against a real OpenAI-compatible server.
//
// The scripted responder (tests/chat_request_test.cpp, tests/chat_turn_test.cpp)
// covers the protocol shapes; what a mock cannot prove is that a *real* model
// reasons, calls tools, and uses what they return. These tests do that, and they are
// registered only when ATUI_LIVE_BASE_URL names a server, so the ordinary self-test
// stays offline, fast and deterministic.
//
//     ATUI_LIVE_BASE_URL=http://10.196.88.170:10001/v1 ./build/atui --self-test
//
// That endpoint is the lab llama.cpp box; its model streams both
// `reasoning_content` and `tool_calls`, which is what the reasoning and tool tests
// need. ATUI_LIVE_MODEL pins a different model (default: whatever the server serves
// first) and ATUI_LIVE_API_KEY sends a bearer token if it needs one. The tests are
// slow by nature -- a 27B model thinks out loud -- so each one waits minutes rather
// than seconds.
//
// The turn is driven the way the OS/2 build drives it on a worker: `start`, then
// `step` until it stops being busy. No threads here, which is also what lets this
// file compile at the language level the target uses -- the version this replaces
// needed <thread> and lambdas, and so could never run on the box that most needs a
// live test.
//
// Network-only: without ATUI_HAVE_NETWORK the body is empty.
#include "harness.h"

#ifdef ATUI_HAVE_NETWORK

#include "net/chat_turn.h"
#include "net/models.h"
#include "net/provider.h"
#include "tools/tool.h"
#include "tools/tool_registry.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace atui_test {

namespace {

// What the probe tool hands back; if it shows up in the model's answer, the
// whole loop (parse call -> execute -> feed result back -> use it) worked.
const char kProbeResult[] = "LIVE_PROBE_RESULT_7741";

std::string envv( const char *name )
{
    const char *v = std::getenv( name );
    return v ? std::string( v ) : std::string();
}

// The server to test against. Empty means "no live server configured", in which
// case init_live_tests() registers nothing at all.
const std::string &liveBase()
{
    static const std::string base = envv( "ATUI_LIVE_BASE_URL" );
    return base;
}

std::string liveKey()
{
    static const std::string key = envv( "ATUI_LIVE_API_KEY" );
    return key;
}

// The model to ask: ATUI_LIVE_MODEL, else the first id the server reports.
std::string resolveModel()
{
    const std::string pinned = envv( "ATUI_LIVE_MODEL" );
    if( !pinned.empty() ) return pinned;

    std::vector<std::string> ids;
    std::string err;
    if( !atui::net::fetchModels( liveBase(), liveKey(), ids, err ) ||
        ids.empty() )
        return std::string();
    return ids[0];
}

const std::string &liveModel()
{
    static const std::string model = resolveModel();
    return model;
}

// A failure with no detail is hard to act on, so say what and where it failed.
void note( const std::string &what )
{
    std::fprintf( stderr, "self-test NOTE live: %s (server %s, model %s)\n",
                  what.c_str(), liveBase().c_str(), liveModel().c_str() );
}

// Points the process-wide provider at the live server. Safe to leave dirty:
// the live tests are registered last, so nothing else runs after them.
void useLive()
{
    atui::net::ProviderSettings &p = atui::net::providerSettings();
    p.baseUrl = liveBase();
    p.apiKey = liveKey();
    p.model = liveModel();
    p.temperature = 0.0;
    p.thinkingResend = false;
    p.models.clear();
}

bool blank( const std::string &s )
{
    for( size_t i = 0; i < s.size(); ++i )
        if( s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r' )
            return false;
    return true;
}

// Everything one streamed exchange produced, split by kind.
struct Streamed
{
    std::string content, thinking, status;
    std::string tool;    // tool records, marked: "call<...>" / "out<...>"
    int questions;       // how often the user would have been asked
};

// The application, as far as the turn is concerned: everything it says is filed by
// kind, and an approval question is counted and answered yes -- these tests are
// asking the server to do the thing, so agreeing to the tool is the point.
class Recorder : public atui::net::ChatTurn::Sink
{
public:
    Streamed *out;
    int questionsAnswered;
    bool finished;
    bool ok;

    Recorder() : out( 0 ), questionsAnswered( 0 ), finished( false ), ok( false ) {}

    virtual void onText( const std::string &piece, bool isThinking )
    {
        if( isThinking ) out->thinking += piece;
        else             out->content  += piece;
    }
    virtual void onStatus( const std::string &line )
    {
        out->status += line;
        out->status += "\n";
    }
    virtual void onToolCall( const std::string &preview )
    {
        out->tool += "call<" + preview + ">\n";
    }
    virtual void onToolNote( const std::string &noteText )
    {
        out->tool += "note<" + noteText + ">\n";
    }
    virtual void onToolResult( const std::string &result )
    {
        out->tool += "out<" + result + ">\n";
    }
    virtual void onApprovalNeeded( const atui::tools::ApprovalRef & )
    {
        ++out->questions;
    }
    virtual void onTurnFinished( bool isOk )
    {
        finished = true;
        ok = isOk;
    }
};

// Runs one exchange to the end. `allow` answers whatever the policy asks about,
// because a live test that declines its own tool call proves nothing.
bool askLive( const std::string &prompt, Streamed &out, bool allow, int timeoutMs )
{
    Recorder rec;
    rec.out = &out;

    atui::net::ChatTurn turn;
    if( !turn.start( prompt, &rec ) )
    {
        note( "the turn would not start: " + prompt );
        return false;
    }

    const int stepMs = 100;
    for( int waited = 0; turn.busy(); waited += stepMs )
    {
        turn.step( stepMs );
        // Approval is delivered through the queue in the app; here it is the same
        // answer given as soon as the question is seen.
        while( rec.questionsAnswered < out.questions )
        {
            turn.answerApproval( allow, false );
            ++rec.questionsAnswered;
        }
        if( waited > timeoutMs )
        {
            note( "timed out waiting for the answer to: " + prompt );
            turn.cancel( "live test timed out" );
            return false;
        }
    }
    return rec.finished;
}

// --- the tests ----------------------------------------------------------------

// GET <base>/models on the real server: the id list atui caches in the config
// and shows in the Providers dialog comes from here.
int test_live_models()
{
    std::vector<std::string> ids;
    std::string err;
    if( !atui::net::fetchModels( liveBase(), liveKey(), ids, err ) )
    {
        note( "model list failed: " + err );
        return -1;
    }
    if( ids.empty() )
    {
        note( "model list is empty" );
        return -2;
    }
    if( liveModel().empty() )
    {
        note( "no usable model id in the list" );
        return -3;
    }
    return 0;
}

// A plain streaming completion: content arrives, the stream ends cleanly.
int test_live_chat_stream()
{
    useLive();
    Streamed got;
    if( !askLive( "Reply with exactly one word: OK", got, true, 300000 ) )
        return -1;
    if( blank( got.content ) )
    {
        note( "no content (status: " + got.status + ")" );
        return -2;
    }
    return 0;
}

// The model thinks out loud, and the thinking must arrive tagged as thinking
// (that is what the transcript's collapsible reasoning blocks are built from).
// Needs a reasoning model: the pinned llama.cpp endpoint is one.
int test_live_reasoning()
{
    useLive();
    Streamed got;
    if( !askLive( "Think it through step by step: what is 17 times 23? "
                  "End your answer with the number.", got, true, 420000 ) )
        return -1;
    if( blank( got.thinking ) )
    {
        note( "no reasoning deltas - is this model a reasoning model?" );
        return -2;
    }
    if( blank( got.content ) )
    {
        note( "reasoning arrived but the answer was empty" );
        return -3;
    }
    return 0;
}

// A tool the test owns, so its reply is known: seeing kProbeResult in the
// model's final answer can only mean the result went back to the model and the
// model used it.
class ProbeTool : public atui::tools::Tool
{
public:
    ProbeTool() : calls( 0 ) {}

    virtual const char *name() const { return "live_probe"; }

    virtual const char *description() const
    {
        return "Returns a fixed probe string. Call this tool whenever the user "
               "asks you to call live_probe.";
    }

    virtual const char *parametersSchema() const
    {
        return "{\"type\":\"object\",\"properties\":{},\"required\":[]}";
    }

    virtual std::string execute( const std::string & )
    {
        ++calls;
        return kProbeResult;
    }

    int calls;
};

int test_live_tool_round_trip()
{
    static ProbeTool probe;                     // registry holds it non-owning
    atui::tools::ToolRegistry::instance().add( &probe );

    useLive();
    Streamed got;
    if( !askLive( "Call the live_probe tool and then tell me exactly what it "
                  "returned.", got, true, 420000 ) )
        return -1;

    if( probe.calls == 0 )
    {
        note( std::string( "the model never called live_probe (status: " +
                           got.status + " answer: " + got.content + " )" ) );
        return -2;
    }
    // The record has to say which tool ran and what it returned: it is the only
    // lasting trace of the call once the (here unnecessary) dialog is gone.
    if( got.tool.find( "call<live_probe" ) == std::string::npos )
    {
        note( "tool ran but no transcript record was reported: " + got.tool );
        return -3;
    }
    if( got.tool.find( kProbeResult ) == std::string::npos )
    {
        note( "transcript record has no result for the call: " + got.tool );
        return -5;
    }
    if( got.content.find( kProbeResult ) == std::string::npos )
    {
        note( std::string( "answer does not use the tool result: " ) +
              got.content );
        return -4;
    }
    return 0;
}

} // anonymous namespace

// Registers the live-server tests, but only when an endpoint was configured.
void init_live_tests()
{
    if( liveBase().empty() ) return;

    register_test( "live: model list",      &test_live_models );
    register_test( "live: chat stream",     &test_live_chat_stream );
    register_test( "live: reasoning",       &test_live_reasoning );
    register_test( "live: tool round trip", &test_live_tool_round_trip );
}

} // namespace atui_test

#endif // ATUI_HAVE_NETWORK
