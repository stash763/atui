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

#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace atui_test {

namespace {

using atui::net::OaiClient;
using atui::net::OaiMessage;
using atui::net::OaiRequest;
using atui::net::OaiResponse;
using atui::net::TokenQueue;

// TokenCallback: append each content token to a std::string.
void appendCb( const char *tok, void *ctx )
{
    if( tok && ctx )
        static_cast< std::string * >( ctx )->append( tok );
}

bool contains( const std::string &hay, const std::string &needle )
{
    return hay.find( needle ) != std::string::npos;
}

void drain( TokenQueue &q, std::string &out )
{
    std::string item;
    while( q.pop( item ) )
        out += item;
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
    drain( q, out );
    if( !contains( out, "~ calling get_time" ) )
        return -3;
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
    register_test( "oai: chat context carry", &test_oai_chat_context_carry );
}

} // namespace atui_test

#endif // ATUI_HAVE_NETWORK
