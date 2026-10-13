// Integration tests for the OpenAI-compatible request path, run against a real
// localhost socket (MockOpenAiServer) so the full stack — buildBody(), libcurl,
// SSE parsing, tool-call reassembly and the OaiChatModel tool loop — is
// exercised without any external network.
//
// Network-only: compiled into the self-test binary but the body is empty when
// ATUI_HAVE_NETWORK is off (so the non-network build links cleanly).
#include "harness.h"

#ifdef ATUI_HAVE_NETWORK

#include "mock_http.h"
#include "net/oai_client.h"
#include "net/oai_chat_model.h"
#include "net/token_queue.h"
#include "net/provider.h"
#include "net/json_extract.h"
#include "tools/approval.h"
#include "tools/permissions.h"
#include "core/journal.h"
#include "net/permission_judge.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace atui_test {

namespace {

using atui::net::OaiClient;
using atui::net::OaiMessage;
using atui::net::OaiRequest;
using atui::net::OaiResponse;
using atui::net::TokenQueue;

// TokenCallback: append answer content tokens (not thinking) to a std::string.
void appendCb( const char *tok, bool thinking, void *ctx )
{
    if( tok && ctx && !thinking )
        static_cast< std::string * >( ctx )->append( tok );
}

// TokenCallback: append only thinking tokens to a std::string.
void appendThinkCb( const char *tok, bool thinking, void *ctx )
{
    if( tok && ctx && thinking )
        static_cast< std::string * >( ctx )->append( tok );
}

bool contains( const std::string &hay, const std::string &needle )
{
    return hay.find( needle ) != std::string::npos;
}

void drain( TokenQueue &q, std::string &out )
{
    atui::net::Token t;
    while( q.pop( t ) )
        out += t.text;
}

// Drain with each token's kind marked, so a test can tell a tool record from
// answer text: "tool>" is the call, "note>" who allowed it and why, "out>" its
// result, "think>" reasoning.
void drainKinds( TokenQueue &q, std::string &out )
{
    atui::net::Token t;
    while( q.pop( t ) )
    {
        switch( t.kind )
        {
            case atui::net::tkThinking:   out += "think>"; break;
            case atui::net::tkStatus:     out += "status>"; break;
            case atui::net::tkTool:       out += "tool>";  break;
            case atui::net::tkToolNote:   out += "note>";  break;
            case atui::net::tkToolResult: out += "out>";   break;
            case atui::net::tkContent:    break;
        }
        out += t.text;
        out += "\n";
    }
}

// Drain capturing only the thinking tokens.
void drainThinking( TokenQueue &q, std::string &out )
{
    atui::net::Token t;
    while( q.pop( t ) )
        if( t.kind == atui::net::tkThinking )
            out += t.text;
}

// Configure the process-wide provider to point at the mock.
void useMock( const MockOpenAiServer &srv )
{
    atui::net::ProviderSettings &p = atui::net::providerSettings();
    p.baseUrl = srv.baseUrl();
    p.apiKey = "";
    p.model = "test-model";
    p.temperature = 0.0;
}

// Wait until pred() is true or we exceed the timeout.
template <typename F>
bool waitFor( F pred, int ms = 10000 )
{
    for( int waited = 0; waited < ms; waited += 20 )
    {
        if( pred() )
            return true;
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    }
    return pred();
}

// --- request body shape -------------------------------------------------------

int test_oai_request_shape()
{
    MockOpenAiServer srv;
    if( !srv.start() )
        return -1;
    srv.enqueue( MockOpenAiServer::sse(
        { "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"ok\"},"
          "\"finish_reason\":\"stop\"}]}" } ) );

    OaiRequest req;
    req.model = "test-model";
    req.temperature = 0.5;
    req.stream = true;
    req.messages.push_back( OaiMessage( "user", "hi there" ) );

    OaiResponse out;
    std::string streamed;
    OaiClient c;
    if( !c.request( srv.baseUrl(), "sk-abc", req, &appendCb, &streamed, &out ) )
        return -1;

    std::string r = srv.lastRequest();
    if( !contains( r, "POST /v1/chat/completions" ) )
        return -2;
    if( !contains( r, "Authorization: Bearer sk-abc" ) )
        return -3;
    if( !contains( r, "Accept: text/event-stream" ) )
        return -4;
    if( !contains( r, "\"model\":\"test-model\"" ) )
        return -5;
    if( !contains( r, "\"stream\":true" ) )
        return -6;
    if( !contains( r, "\"temperature\":0.5" ) ) // std::to_string -> 0.500000
        return -7;
    if( !contains( r, "\"role\":\"user\"" ) )
        return -8;
    if( !contains( r, "\"content\":\"hi there\"" ) )
        return -9;
    if( contains( r, "\"tools\"" ) )
        return -10; // no tools offered -> key must be absent

    srv.stop();
    return 0;
}

int test_oai_tools_serialized()
{
    MockOpenAiServer srv;
    if( !srv.start() )
        return -1;
    srv.enqueue( MockOpenAiServer::sse(
        { "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"x\"},"
          "\"finish_reason\":\"stop\"}]}" } ) );

    OaiRequest req;
    req.model = "m";
    req.messages.push_back( OaiMessage( "user", "t" ) );
    req.toolsJson =
        "[{\"type\":\"function\",\"function\":{\"name\":\"get_time\","
        "\"parameters\":{\"type\":\"object\",\"properties\":{}}}}]";

    OaiClient c;
    if( !c.request( srv.baseUrl(), "", req, 0, 0, 0 ) )
        return -1;
    std::string r = srv.lastRequest();
    if( !contains( r, "\"tools\":[" ) )
        return -2;
    if( !contains( r, "\"tool_choice\":\"auto\"" ) )
        return -3;
    if( !contains( r, "\"name\":\"get_time\"" ) )
        return -4;
    srv.stop();
    return 0;
}

// --- streamed content ---------------------------------------------------------

int test_oai_content_stream()
{
    MockOpenAiServer srv;
    if( !srv.start() )
        return -1;
    srv.enqueue( MockOpenAiServer::sse( {
        "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hello \"}}]}",
        "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"world\"},"
        "\"finish_reason\":\"stop\"}]}" } ) );

    OaiRequest req;
    req.model = "m";
    req.messages.push_back( OaiMessage( "user", "hi" ) );
    OaiResponse out;
    std::string streamed;
    OaiClient c;
    if( !c.request( srv.baseUrl(), "", req, &appendCb, &streamed, &out ) )
        return -1;

    if( out.content != "Hello world" )
        return -2;
    if( out.finishReason != "stop" )
        return -3;
    if( streamed != "Hello world" )
        return -4;
    if( !out.toolCalls.empty() )
        return -5;
    srv.stop();
    return 0;
}

// --- streaming tool-call reassembly ------------------------------------------

int test_oai_tool_calls()
{
    MockOpenAiServer srv;
    if( !srv.start() )
        return -1;
    // Arguments arrive split across two deltas: "[1" then ",2]".
    srv.enqueue( MockOpenAiServer::sse( {
        "{\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\","
        "\"tool_calls\":[{\"index\":0,\"id\":\"call_zz\",\"type\":\"function\","
        "\"function\":{\"name\":\"get_time\",\"arguments\":\"[1\"}}]}}]}",
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,"
        "\"function\":{\"arguments\":\",2]\"}}]},\"finish_reason\":\"tool_calls\"}]}" } ) );

    OaiRequest req;
    req.model = "m";
    req.messages.push_back( OaiMessage( "user", "time?" ) );
    OaiResponse out;
    std::string streamed;
    OaiClient c;
    if( !c.request( srv.baseUrl(), "", req, &appendCb, &streamed, &out ) )
        return -1;

    if( out.finishReason != "tool_calls" )
        return -2;
    if( out.toolCalls.size() != 1 )
        return -3;
    if( out.toolCalls[0].id != "call_zz" )
        return -4;
    if( out.toolCalls[0].name != "get_time" )
        return -5;
    if( out.toolCalls[0].arguments != "[1,2]" )
        return -6;
    srv.stop();
    return 0;
}

// --- assistant tool_calls + tool result serialization ------------------------

int test_oai_message_serialization()
{
    MockOpenAiServer srv;
    if( !srv.start() )
        return -1;
    srv.enqueue( MockOpenAiServer::sse(
        { "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"k\"},"
          "\"finish_reason\":\"stop\"}]}" } ) );

    OaiRequest req;
    req.model = "m";
    // An assistant turn that requested a tool (no text content -> null).
    OaiMessage asst( "assistant", "" );
    atui::net::OaiToolCall tc;
    tc.id = "call_1";
    tc.name = "get_time";
    tc.arguments = "{}";
    asst.toolCalls.push_back( tc );
    req.messages.push_back( asst );
    // The matching tool result.
    OaiMessage tr( "tool", "12:34:56" );
    tr.toolCallId = "call_1";
    tr.name = "get_time";
    req.messages.push_back( tr );

    OaiClient c;
    if( !c.request( srv.baseUrl(), "", req, 0, 0, 0 ) )
        return -1;
    std::string r = srv.lastRequest();
    if( !contains( r, "\"content\":null" ) )
        return -2; // assistant with only tool_calls
    if( !contains( r, "\"tool_calls\":[{\"id\":\"call_1\"" ) )
        return -3;
    if( !contains( r, "\"role\":\"tool\"" ) )
        return -4;
    if( !contains( r, "\"tool_call_id\":\"call_1\"" ) )
        return -5;
    srv.stop();
    return 0;
}

// --- transport failure --------------------------------------------------------

int test_oai_connection_refused()
{
    // Port 9 (discard) on loopback is not listening -> curl fails to connect.
    OaiRequest req;
    req.model = "m";
    req.messages.push_back( OaiMessage( "user", "x" ) );
    OaiClient c;
    if( c.request( "http://127.0.0.1:9/v1", "", req, 0, 0, 0 ) )
        return -1; // must report failure
    return 0;
}

// --- OaiChatModel tool loop (end to end) --------------------------------------

int test_oai_chat_tool_loop()
{
    MockOpenAiServer srv;
    if( !srv.start() )
        return -1;
    // Round 0: ask for get_time. Round 1: final answer.
    srv.enqueue( MockOpenAiServer::sse( {
        "{\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\","
        "\"tool_calls\":[{\"index\":0,\"id\":\"call_t\",\"type\":\"function\","
        "\"function\":{\"name\":\"get_time\",\"arguments\":\"{}\"}}]}},"
        "\"finish_reason\":\"tool_calls\"}]}" } ) );
    srv.enqueue( MockOpenAiServer::sse( {
        "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"It is high noon\"}},"
        "{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}" } ) );

    useMock( srv );
    TokenQueue q;
    atui::net::OaiChatModel model;
    if( !model.ask( "what time?", "", &q ) )
        return -1;
    if( !waitFor( [&] { return model.isDone(); } ) )
        return -2;
    std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

    std::string out;
    drainKinds( q, out );
    // The call must be recorded as a tool record (with what it returned), not
    // just a "calling ..." status line: that record is the transcript's only
    // lasting evidence of what the agent actually did.
    if( !contains( out, "tool>get_time" ) )
        return -3;
    if( !contains( out, "out>" ) )
        return -10;
    if( !contains( out, "It is high noon" ) )
        return -4;

    std::vector<std::string> reqs = srv.requests();
    if( reqs.size() != 2 )
        return -5;
    // Second request must carry the assistant tool_calls turn and the tool
    // result the model asked for.
    const std::string &r2 = reqs[1];
    if( !contains( r2, "\"tool_calls\":" ) )
        return -6;
    if( !contains( r2, "\"role\":\"tool\"" ) )
        return -7;
    if( !contains( r2, "\"tool_call_id\":\"call_t\"" ) )
        return -8;
    if( !contains( r2, "\"name\":\"get_time\"" ) )
        return -9;
    srv.stop();
    return 0;
}

// --- OaiChatModel multi-turn context carry ------------------------------------

int test_oai_chat_context_carry()
{
    MockOpenAiServer srv;
    if( !srv.start() )
        return -1;
    srv.enqueue( MockOpenAiServer::sse(
        { "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Alpha\"},"
          "\"finish_reason\":\"stop\"}]}" } ) );
    srv.enqueue( MockOpenAiServer::sse(
        { "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Beta\"},"
          "\"finish_reason\":\"stop\"}]}" } ) );

    useMock( srv );
    TokenQueue q;
    atui::net::OaiChatModel model;
    if( !model.ask( "one", "", &q ) )
        return -1;
    if( !waitFor( [&] { return model.isDone(); } ) )
        return -2;
    if( !model.ask( "two", "", &q ) )
        return -3;
    if( !waitFor( [&] { return model.isDone(); } ) )
        return -4;
    std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

    std::vector<std::string> reqs = srv.requests();
    if( reqs.size() != 2 )
        return -5;
    // Second request must replay turn 1 (user "one", assistant "Alpha") plus
    // the new user turn "two".
    const std::string &r2 = reqs[1];
    if( !contains( r2, "one" ) || !contains( r2, "Alpha" ) ||
        !contains( r2, "two" ) )
        return -6;
    srv.stop();
    return 0;
}

// --- thinking / reasoning stream ---------------------------------------------

struct Cap { std::string content; std::string thinking; };

void capCb( const char *tok, bool thinking, void *ctx )
{
    if( !tok || !ctx ) return;
    Cap *c = static_cast< Cap * >( ctx );
    if( thinking ) c->thinking += tok;
    else           c->content  += tok;
}

int test_oai_delta_reasoning_parse()
{
    // Ollama-style "reasoning".
    if( atui::net::deltaReasoning(
            "{\"choices\":[{\"index\":0,\"delta\":{\"reasoning\":\"abc\"}}]}" )
        != "abc" )
        return -1;
    // DeepSeek-style "reasoning_content".
    if( atui::net::deltaReasoning(
            "{\"choices\":[{\"index\":0,\"delta\":{\"reasoning_content\":\"xyz\"}}]}" )
        != "xyz" )
        return -2;
    // Neither present.
    if( !atui::net::deltaReasoning(
            "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"hi\"}}]}" ).empty() )
        return -3;
    // Explicit null.
    if( !atui::net::deltaReasoning(
            "{\"choices\":[{\"index\":0,\"delta\":{\"reasoning\":null}}]}" ).empty() )
        return -4;
    return 0;
}

int test_oai_reasoning_stream()
{
    MockOpenAiServer srv;
    if( !srv.start() )
        return -1;
    srv.enqueue( MockOpenAiServer::sse( {
        "{\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\","
        "\"content\":\"\",\"reasoning\":\"Step 1: think\"}}]}",
        "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Answer\"}}]}",
        "{\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}" } ) );

    OaiRequest req;
    req.model = "m";
    req.messages.push_back( OaiMessage( "user", "why?" ) );
    OaiResponse out;
    Cap cap;
    OaiClient c;
    if( !c.request( srv.baseUrl(), "", req, &capCb, &cap, &out ) )
        return -1;

    if( out.content != "Answer" )
        return -2;
    if( out.reasoning != "Step 1: think" )
        return -3;
    if( out.finishReason != "stop" )
        return -4;
    // Callback routing: content vs thinking kept separate.
    if( cap.content != "Answer" )
        return -5;
    if( cap.thinking != "Step 1: think" )
        return -6;
    srv.stop();
    return 0;
}

int test_oai_chat_thinking()
{
    MockOpenAiServer srv;
    if( !srv.start() )
        return -1;
    srv.enqueue( MockOpenAiServer::sse( {
        "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"\",\"reasoning\":\"pondering\"}}]}",
        "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"the answer\"},"
        "\"finish_reason\":\"stop\"}]}" } ) );

    useMock( srv );
    TokenQueue q;
    atui::net::OaiChatModel model;
    if( !model.ask( "think about it", "", &q ) )
        return -1;
    if( !waitFor( [&] { return model.isDone(); } ) )
        return -2;
    std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

    // Drain, separating thinking from content by token kind.
    std::string thinking, content;
    atui::net::Token t;
    while( q.pop( t ) )
    {
        if( t.kind == atui::net::tkThinking ) thinking += t.text;
        else if( t.kind == atui::net::tkContent ) content += t.text;
    }
    if( thinking != "pondering" )
        return -3;
    if( content != "the answer" )
        return -4;
    srv.stop();
    return 0;
}

// Run two turns against the mock with the given resend setting; return the
// recorded requests. Turn 1's response carries thinking text.
int runTwoTurns( bool resend, std::vector<std::string> &reqsOut )
{
    MockOpenAiServer srv;
    if( !srv.start() )
        return -1;
    srv.enqueue( MockOpenAiServer::sse( {
        "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"\",\"reasoning\":\"think about it\"}}]}",
        "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"First answer\"},"
        "\"finish_reason\":\"stop\"}]}" } ) );
    srv.enqueue( MockOpenAiServer::sse(
        { "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Second\"},"
          "\"finish_reason\":\"stop\"}]}" } ) );

    useMock( srv );
    atui::net::providerSettings().thinkingResend = resend;

    TokenQueue q;
    atui::net::OaiChatModel model;
    if( !model.ask( "q1", "", &q ) )
        return -2;
    if( !waitFor( [&] { return model.isDone(); } ) )
        return -3;
    if( !model.ask( "q2", "", &q ) )
        return -4;
    if( !waitFor( [&] { return model.isDone(); } ) )
        return -5;
    std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

    reqsOut = srv.requests();
    srv.stop();
    return 0;
}

int test_oai_chat_resend_off()
{
    std::vector<std::string> reqs;
    int rc = runTwoTurns( false, reqs );
    if( rc != 0 )
        return rc;
    if( reqs.size() != 2 )
        return -6;
    // Turn 2 replays turn 1's answer but NOT its thinking.
    if( !contains( reqs[1], "First answer" ) )
        return -7;
    if( contains( reqs[1], "reasoning_content" ) )
        return -8;
    return 0;
}

int test_oai_chat_resend_on()
{
    std::vector<std::string> reqs;
    int rc = runTwoTurns( true, reqs );
    if( rc != 0 )
        return rc;
    if( reqs.size() != 2 )
        return -6;
    if( !contains( reqs[1], "reasoning_content" ) )
        return -7;
    if( !contains( reqs[1], "think about it" ) )
        return -8;
    return 0;
}

// --- model list (GET /models) -------------------------------------------------

int test_oai_model_ids_parse()
{
    const char *body =
        "{\"object\":\"list\",\"data\":["
        "{\"id\":\"llama3:latest\",\"object\":\"model\",\"owned_by\":\"library\"},"
        "{\"id\":\"hf.co/x/Y:BF16\",\"object\":\"model\"},"
        "{\"id\":\"phi4-mini:3.8b\",\"object\":\"model\"}"
        "]}";
    std::vector<std::string> ids = atui::net::modelIds( body );
    if( ids.size() != 3 ) return -1;
    if( ids[0] != "llama3:latest" )   return -2;
    if( ids[1] != "hf.co/x/Y:BF16" )  return -3;
    if( ids[2] != "phi4-mini:3.8b" )  return -4;

    if( !atui::net::modelIds( "{\"error\":\"nope\"}" ).empty() ) return -5;
    if( !atui::net::modelIds( "{\"data\":[]}" ).empty() )        return -6;
    // An "id" outside a data array must be ignored.
    if( !atui::net::modelIds( "{\"id\":\"top\"}" ).empty() )     return -7;
    return 0;
}

int test_oai_fetch_models()
{
    MockOpenAiServer srv;
    if( !srv.start() )
        return -1;
    srv.enqueue(
        "{\"object\":\"list\",\"data\":["
        "{\"id\":\"granite4:latest\"},{\"id\":\"phi4-mini:3.8b\"}]}" );

    std::vector<std::string> ids;
    std::string err;
    if( !atui::net::fetchModels( srv.baseUrl(), "", ids, err ) )
        return -2;
    if( !contains( srv.lastRequest(), "GET /v1/models" ) )
        return -3;
    if( ids.size() != 2 )             return -4;
    if( ids[0] != "granite4:latest" ) return -5;
    if( ids[1] != "phi4-mini:3.8b" )  return -6;

    // Empty base URL -> error, no request made.
    ids.clear();
    if( atui::net::fetchModels( "", "", ids, err ) )
        return -7;
    if( err.empty() )
        return -8;
    return 0;
}

// --- sensitive-tool approval in the chat loop --------------------------------

// A test Approver answers with a fixed decision and records each request.
struct TestApprover : public atui::tools::Approver
{
    bool decision;
    bool session;          // answer "yes, and for the rest of the run"
    int  calls;
    std::string lastTool;
    std::string lastSummary;
    std::string lastReason;   // what the policy said it could not clear
    TestApprover() : decision( true ), session( false ), calls( 0 ) {}
    virtual bool approve( const atui::tools::ApprovalRef &req )
    {
        ++calls;
        lastTool    = req->tool;
        lastSummary = req->summary;
        lastReason  = req->reason;
        req->answer( decision, session );   // as the real UI thread would
        return decision;
    }
};

// Drive one ask() whose round 0 asks to run_command; `allow` decides approval.
int runApprovalRound( bool allow, std::string &drained,
                      std::string &req2, int &calls )
{
    MockOpenAiServer srv;
    if( !srv.start() )
        return -1;
    // Round 0: request run_command(echo hi). Round 1: final answer.
    srv.enqueue( MockOpenAiServer::sse( {
        "{\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\","
        "\"tool_calls\":[{\"index\":0,\"id\":\"call_r\",\"type\":\"function\","
        "\"function\":{\"name\":\"run_command\","
        "\"arguments\":\"{\\\"command\\\":\\\"echo hi\\\"}\"}}]}},"
        "\"finish_reason\":\"tool_calls\"}]}" } ) );
    srv.enqueue( MockOpenAiServer::sse( {
        "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"all done\","
        "\"finish_reason\":\"stop\"}]}" } ) );

    useMock( srv );
    TestApprover ap;
    ap.decision = allow;
    atui::tools::setApprover( &ap );

    TokenQueue q;
    atui::net::OaiChatModel model;
    int rc = 0;
    if( !model.ask( "run it", "", &q ) )
        rc = -2;
    else if( !waitFor( [&] { return model.isDone(); } ) )
        rc = -3;
    else
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        drainKinds( q, drained );
        calls = ap.calls;
        std::vector<std::string> reqs = srv.requests();
        if( reqs.size() == 2 )
            req2 = reqs[1];
        else
            rc = -4;
    }

    atui::tools::setApprover( 0 );   // restore: no approver outside tests
    srv.stop();
    return rc;
}

int test_oai_tool_approval_allow()
{
    std::string drained, req2; int calls = 0;
    int rc = runApprovalRound( true, drained, req2, calls );
    if( rc != 0 )
        return rc;
    if( calls != 1 )
        return -5;
    // The record shows the command that ran, then its output.
    if( !contains( drained, "tool>echo hi" ) )
        return -6;
    if( !contains( drained, "out>exit code 0" ) )
        return -10;
    if( !contains( drained, "all done" ) )
        return -7;
    // Round 1 replays the tool result: the command actually ran (exit code 0).
    if( !contains( req2, "\"role\":\"tool\"" ) )
        return -8;
    if( !contains( req2, "exit code 0" ) )
        return -9;
    return 0;
}

int test_oai_tool_approval_deny()
{
    std::string drained, req2; int calls = 0;
    int rc = runApprovalRound( false, drained, req2, calls );
    if( rc != 0 )
        return rc;
    if( calls != 1 )
        return -5;
    // A declined call is still recorded - with the command and the refusal -
    // so the transcript never shows an unexplained gap.
    if( !contains( drained, "tool>echo hi" ) )
        return -6;
    if( !contains( drained, "out>The user declined to run 'run_command'." ) )
        return -10;
    if( contains( drained, "exit code" ) )
        return -7;   // must NOT have run
    if( !contains( drained, "all done" ) )
        return -8;
    // The tool result the model sees reports the denial.
    if( !contains( req2, "declined" ) )
        return -9;
    return 0;
}

// --- permission modes in the chat loop -----------------------------------------
//
// tools/permissions.cpp is tested rung by rung in selftest.cpp; what is tested
// here is the seam: that the loop asks the policy at all, that a call the policy
// clears never reaches the approver, that the transcript says who answered, and
// that every way the agent can fail ends in a question rather than a run.

// Restores the process-wide permission state, and starts each test with no
// cached judgements and no session allowances, so tests cannot lean on each
// other through the caches.
class PermissionReset
{
public:
    PermissionReset()
        : mode_( atui::tools::permissionMode() ),
          root_( atui::tools::projectRoot() )
    {
        atui::tools::setPermissionJudge( 0 );
        atui::tools::clearPlanCache();
        atui::tools::clearAllowed();
    }
    ~PermissionReset()
    {
        atui::tools::setPermissionJudge( 0 );
        atui::tools::permissionMode() = mode_;
        atui::tools::setProjectRoot( root_ );
        atui::tools::clearPlanCache();
        atui::tools::clearAllowed();
    }

private:
    atui::tools::PermissionMode mode_;
    std::string root_;
};

// A scratch project: <tmp>/atui_perm_mock/proj with one text file in it.
// edit_file only edits files that exist, and containment is only worth testing
// against directories that do.
class ScratchProject
{
public:
    std::string base;
    std::string root;
    std::string file;
    bool ok;
    std::string savedJournalDir;
    bool savedJournalOn;

    ScratchProject() : ok( false )
    {
        const char *tmp = std::getenv( "TMPDIR" );
        if( !tmp || !*tmp ) tmp = "/tmp";
        base = std::string( tmp );
        while( base.size() > 1 && base[ base.size() - 1 ] == '/' )
            base.erase( base.size() - 1 );
        base += "/atui_perm_mock";
        ::mkdir( base.c_str(), 0755 );   // may already exist

        root = base + "/proj";
        ::mkdir( root.c_str(), 0755 );
        file = root + "/target.txt";
        std::ofstream out( file.c_str() );
        out << "alpha\n";
        if( !out ) return;
        out.close();

        atui::tools::setProjectRoot( root );
        root = atui::tools::projectRoot();
        ok   = ( !root.empty() && root.find( "atui_perm_mock" ) != std::string::npos );

        // The undo journal the edits go through is the real one, so it needs a
        // directory this test owns: pointing it at the default (the install
        // directory) would leave entries behind in whatever tree atui happened to
        // be built in.
        savedJournalDir = atui::core::journal::directory();
        savedJournalOn = atui::core::journal::enabled();
        atui::core::journal::directory() = base + "/undo";
        atui::core::journal::enabled() = true;
        ::mkdir( atui::core::journal::directory().c_str(), 0755 );
    }

    ~ScratchProject()
    {
        std::vector< atui::core::journal::Entry > entries;
        atui::core::journal::entries( entries );
        for( std::size_t i = 0; i < entries.size(); ++i )
        {
            std::remove( entries[i].preImage.c_str() );
            std::remove( entries[i].metaPath.c_str() );
        }
        ::rmdir( atui::core::journal::directory().c_str() );
        atui::core::journal::directory() = savedJournalDir;
        atui::core::journal::enabled()   = savedJournalOn;

        std::remove( file.c_str() );
        ::rmdir( root.c_str() );
        ::rmdir( base.c_str() );
    }

    std::string contents() const
    {
        std::ifstream in( file.c_str() );
        return std::string( (std::istreambuf_iterator<char>( in )),
                            std::istreambuf_iterator<char>() );
    }
};

// A judge that answers from a canned reply: no HTTP, no latency, and the same
// parse path the real one uses, so a wrong contract shows up here rather than in
// front of the user.
class FakeJudge : public atui::tools::PermissionJudge
{
public:
    std::string reply;
    int calls;
    FakeJudge() : reply( "SUMMARY harmless\nNET no" ), calls( 0 ) {}
    virtual void plan( const std::string &, const std::string &,
                       atui::tools::EffectPlan &out )
    {
        ++calls;
        atui::net::parseEffectPlan( reply, out );
    }
};

// Quotes a JSON blob for the "arguments" string of a tool_call payload.
std::string payloadQuote( const std::string &json )
{
    std::string out;
    for( std::size_t i = 0; i < json.size(); ++i )
    {
        const char c = json[i];
        if( c == '"' )       out += "\\\"";
        else if( c == '\\' ) out += "\\\\";
        else                 out += c;
    }
    return out;
}

// One round 0 that asks for `toolName`( `args` ), optionally twice over with the
// identical arguments.
std::string toolCallRound( const std::string &toolName,
                           const std::string &args, bool twice )
{
    std::string calls =
        "{\"index\":0,\"id\":\"c1\",\"type\":\"function\",\"function\":{"
        "\"name\":\"" + toolName + "\",\"arguments\":\"" +
        payloadQuote( args ) + "\"}}";
    if( twice )
        calls +=
            ",{\"index\":1,\"id\":\"c2\",\"type\":\"function\",\"function\":{"
            "\"name\":\"" + toolName + "\",\"arguments\":\"" +
            payloadQuote( args ) + "\"}}";

    return std::string( "{\"choices\":[{\"index\":0,\"delta\":{\"role\":"
                        "\"assistant\",\"tool_calls\":[" ) + calls +
                        "]}},\"finish_reason\":\"tool_calls\"}]}";
}

// Drives one ask() whose first round requests a single tool, with the approver
// answering `allow` (and `session` for identical calls later).
int runPermissionRound( const std::string &toolName, const std::string &args,
                        bool twice, bool allow, bool session,
                        std::string &drained, int &approvals )
{
    MockOpenAiServer srv;
    if( !srv.start() )
        return -1;
    srv.enqueue( MockOpenAiServer::sse( { toolCallRound( toolName, args,
                                                         twice ) } ) );
    srv.enqueue( MockOpenAiServer::sse( {
        "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"all done\","
        "\"finish_reason\":\"stop\"}]}" } ) );

    useMock( srv );
    TestApprover ap;
    ap.decision = allow;
    ap.session  = session;
    atui::tools::setApprover( &ap );

    int rc = 0;
    {
        TokenQueue q;
        atui::net::OaiChatModel model;
        if( !model.ask( "do it", "", &q ) )
            rc = -2;
        else if( !waitFor( [&] { return model.isDone(); } ) )
            rc = -3;
        else
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            drainKinds( q, drained );
        }
    }

    approvals = ap.calls;
    atui::tools::setApprover( 0 );
    srv.stop();
    return rc;
}

// exact mode: a file inside the project is decided by containment, so the user
// is never asked and the edit still happens.
int test_oai_exact_mode_edits_in_tree()
{
    PermissionReset reset;
    ScratchProject tree;
    if( !tree.ok )
        return -1;
    atui::tools::permissionMode() = atui::tools::pmExact;

    std::string drained; int approvals = 0;
    const int rc = runPermissionRound(
        "edit_file",
        "{\"path\":\"" + tree.file + "\",\"old_text\":\"alpha\","
        "\"new_text\":\"omega\"}",
        false, true, false, drained, approvals );
    if( rc != 0 )
        return rc;
    if( approvals != 0 )
        return -2;   // asking means the policy did not decide
    if( !contains( drained, "note>auto: inside the project" ) )
        return -3;
    if( tree.contents().find( "omega" ) == std::string::npos )
        return -4;   // it must actually have run
    return 0;
}

// exact mode: the same mode still asks about a command, because the facts cannot
// say what `echo` reaches, and says that in the reason.
int test_oai_exact_mode_asks_about_commands()
{
    PermissionReset reset;
    ScratchProject tree;
    if( !tree.ok )
        return -1;
    atui::tools::permissionMode() = atui::tools::pmExact;

    std::string drained; int approvals = 0;
    int rc = runPermissionRound( "run_command",
                                 "{\"command\":\"echo hi\"}",
                                 false, true, false, drained, approvals );
    if( rc != 0 )
        return rc;
    if( approvals != 1 )
        return -2;
    if( !contains( drained, "note>nothing outside the project" ) )
        return -3;
    if( !contains( drained, "(you allowed it)" ) )
        return -4;
    if( !contains( drained, "out>exit code 0" ) )
        return -5;
    return 0;
}

// agent mode: a described, contained command runs with the agent's own words in
// the record, and the user is not interrupted.
int test_oai_agent_clears_described_command()
{
    PermissionReset reset;
    ScratchProject tree;
    if( !tree.ok )
        return -1;
    atui::tools::permissionMode() = atui::tools::pmAgent;
    FakeJudge judge;
    judge.reply = "SUMMARY prints a greeting\nNET no";
    atui::tools::setPermissionJudge( &judge );

    std::string drained; int approvals = 0;
    const int rc = runPermissionRound( "run_command",
                                       "{\"command\":\"echo hi\"}",
                                       false, true, false, drained, approvals );
    if( rc != 0 )
        return rc;
    if( judge.calls != 1 )
        return -2;
    if( approvals != 0 )
        return -3;
    if( !contains( drained, "note>agent: prints a greeting" ) )
        return -4;
    if( !contains( drained, "out>exit code 0" ) )
        return -5;
    return 0;
}

// agent mode, twice the same call: one question to the model, not two. A
// permission agent that costs a round-trip per keystroke of a build loop is a
// permission agent that gets turned off.
int test_oai_agent_answers_once_per_call()
{
    PermissionReset reset;
    ScratchProject tree;
    if( !tree.ok )
        return -1;
    atui::tools::permissionMode() = atui::tools::pmAgent;
    FakeJudge judge;
    judge.reply = "SUMMARY prints a greeting\nNET no";
    atui::tools::setPermissionJudge( &judge );

    std::string drained; int approvals = 0;
    const int rc = runPermissionRound( "run_command",
                                       "{\"command\":\"echo hi\"}",
                                       true, true, false, drained, approvals );
    if( rc != 0 )
        return rc;
    if( judge.calls != 1 )
        return -2;   // the second identical call came from the cache
    if( approvals != 0 )
        return -3;
    if( !contains( drained, "as before" ) )
        return -4;   // the record says the answer was reused
    return 0;
}

// An agent that reports privileges is a question, not a run.
int test_oai_agent_flagged_effect_asks()
{
    PermissionReset reset;
    ScratchProject tree;
    if( !tree.ok )
        return -1;
    atui::tools::permissionMode() = atui::tools::pmAgent;
    FakeJudge judge;
    judge.reply = "SUMMARY installs a system package\nELEVATED yes";
    atui::tools::setPermissionJudge( &judge );

    std::string drained; int approvals = 0;
    TestApprover ap;
    ap.decision = false;
    atui::tools::setApprover( &ap );

    MockOpenAiServer srv;
    if( !srv.start() )
        return -2;
    srv.enqueue( MockOpenAiServer::sse( { toolCallRound(
        "run_command", "{\"command\":\"echo hi\"}", false ) } ) );
    srv.enqueue( MockOpenAiServer::sse( {
        "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"ok\","
        "\"finish_reason\":\"stop\"}]}" } ) );
    useMock( srv );

    TokenQueue q;
    atui::net::OaiChatModel model;
    if( !model.ask( "do it", "", &q ) )
        return -3;
    if( !waitFor( [&] { return model.isDone(); } ) )
        return -4;
    std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    drainKinds( q, drained );
    srv.stop();

    approvals = ap.calls;
    atui::tools::setApprover( 0 );
    if( approvals != 1 )
        return -5;
    if( !contains( ap.lastReason, "elevated" ) )
        return -6;
    if( !contains( drained, "(you declined)" ) )
        return -7;
    if( contains( drained, "exit code" ) )
        return -8;   // must not have run
    return 0;
}

// An answer that cannot be read is not a description: fail to a question.
int test_oai_agent_unreadable_reply_asks()
{
    PermissionReset reset;
    ScratchProject tree;
    if( !tree.ok )
        return -1;
    atui::tools::permissionMode() = atui::tools::pmAgent;
    FakeJudge judge;
    judge.reply = "I'm sorry, but I cannot determine what that does.";
    atui::tools::setPermissionJudge( &judge );

    std::string drained; int approvals = 0;
    int rc = runPermissionRound( "run_command",
                                 "{\"command\":\"echo hi\"}",
                                 false, false, false, drained, approvals );
    if( rc != 0 )
        return rc;
    if( approvals != 1 )
        return -2;
    if( !contains( drained, "could not describe" ) )
        return -3;
    if( contains( drained, "exit code" ) )
        return -4;
    return 0;
}

// "Allow this session" outranks the policy for the identical call and only that
// call: the second copy is not asked about, and nothing else is covered.
int test_oai_allow_this_session_is_exact()
{
    PermissionReset reset;
    ScratchProject tree;
    if( !tree.ok )
        return -1;
    atui::tools::permissionMode() = atui::tools::pmAskAll;

    std::string drained; int approvals = 0;
    int rc = runPermissionRound( "run_command",
                                 "{\"command\":\"echo hi\"}",
                                 true, true, true, drained, approvals );
    if( rc != 0 )
        return rc;
    if( approvals != 1 )
        return -2;   // asked once, not twice
    if( !contains( drained, "note>allowed for this session" ) )
        return -3;
    std::size_t first = drained.find( "exit code 0" );
    if( first == std::string::npos )
        return -4;
    if( drained.find( "exit code 0", first + 1 ) == std::string::npos )
        return -5;   // both calls ran
    return 0;
}

} // anonymous namespace

void init_network_tests()
{
    register_test( "oai: request shape", &test_oai_request_shape );
    register_test( "oai: tools serialized", &test_oai_tools_serialized );
    register_test( "oai: content stream", &test_oai_content_stream );
    register_test( "oai: tool calls", &test_oai_tool_calls );
    register_test( "oai: message serialization", &test_oai_message_serialization );
    register_test( "oai: connection refused", &test_oai_connection_refused );
    register_test( "oai: chat tool loop", &test_oai_chat_tool_loop );
    register_test( "oai: exact mode edits in tree",
                   &test_oai_exact_mode_edits_in_tree );
    register_test( "oai: exact mode asks about commands",
                   &test_oai_exact_mode_asks_about_commands );
    register_test( "oai: agent clears described command",
                   &test_oai_agent_clears_described_command );
    register_test( "oai: agent answers once per call",
                   &test_oai_agent_answers_once_per_call );
    register_test( "oai: agent flagged effect asks",
                   &test_oai_agent_flagged_effect_asks );
    register_test( "oai: agent unreadable reply asks",
                   &test_oai_agent_unreadable_reply_asks );
    register_test( "oai: allow this session is exact",
                   &test_oai_allow_this_session_is_exact );
    register_test( "oai: chat context carry", &test_oai_chat_context_carry );
    register_test( "oai: delta reasoning parse", &test_oai_delta_reasoning_parse );
    register_test( "oai: reasoning stream", &test_oai_reasoning_stream );
    register_test( "oai: chat thinking", &test_oai_chat_thinking );
    register_test( "oai: chat resend off", &test_oai_chat_resend_off );
    register_test( "oai: chat resend on", &test_oai_chat_resend_on );
    register_test( "oai: model ids parse", &test_oai_model_ids_parse );
    register_test( "oai: fetch models", &test_oai_fetch_models );
    register_test( "oai: tool approval allow", &test_oai_tool_approval_allow );
    register_test( "oai: tool approval deny", &test_oai_tool_approval_deny );
}

} // namespace atui_test

#endif // ATUI_HAVE_NETWORK
