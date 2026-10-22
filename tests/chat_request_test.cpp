// Tests for one streaming chat completion (ChatRequest) and the SSE framing
// underneath it, run against the scripted responder in http_pipe.h -- so the answer
// can arrive one byte at a time, end at `[DONE]` while the socket stays open, or
// come back as a 401 with a sentence explaining it.
//
// Network-only: nothing here is compiled when ATUI_HAVE_NETWORK is off.
#include "harness.h"

#ifdef ATUI_HAVE_NETWORK

#include "http_pipe.h"
#include "sse_script.h"

#include "net/chat_request.h"
#include "net/sse.h"

#include <chrono>
#include <thread>
#include <cstdio>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

namespace atui_test {

namespace {

// A whole answer, in the shape this file's tests want: one content delta, `stop`,
// and the end of the stream.
std::string sseBody( const std::string &answer )
{
    return atui_test::sseAnswer( answer );
}

using atui::net::ChatRequest;
using atui::net::OaiMessage;
using atui::net::OaiRequest;
using atui::net::SseStream;

// --- the SSE framing ------------------------------------------------------------

// Records payloads in the order they arrive.
struct PayloadSink : public SseStream::Sink
{
    std::vector<std::string> payloads;
    int dones;

    PayloadSink() : dones( 0 ) {}
    virtual void onPayload( const std::string &payload )
    {
        payloads.push_back( payload );
    }
    virtual void onDone() { ++dones; }

    std::string joined() const
    {
        std::string out;
        for( std::size_t i = 0; i < payloads.size(); ++i )
        {
            if( i )
                out += "|";
            out += payloads[i];
        }
        return out;
    }
};

// Feeds `text` to a stream in pieces of `chunk` bytes and returns what came out.
// Every test below runs the same bytes through several chunk sizes, because the
// framing is exactly the code that has to not care.
static PayloadSink feed( const std::string &text, std::size_t chunk )
{
    PayloadSink sink;
    SseStream sse;
    for( std::size_t at = 0; at < text.size(); at += chunk )
    {
        sse.feed( text.c_str() + at, chunk, sink );
        if( sse.done() )
            break;
    }
    // Whatever is left in the stream's buffer is an incomplete line, which is the
    // point: no payload should have been emitted for it.
    return sink;
}

int test_sse_one_payload_per_line()
{
    if( feed( "data: one\n\ndata: two\n\n", 4096 ).joined() != "one|two" )
        return -1;
    return 0;
}

int test_sse_split_at_every_size()
{
    const std::string stream =
        "data: hello\n\ndata: world\n\n";
    for( std::size_t chunk = 1; chunk <= stream.size(); ++chunk )
    {
        const PayloadSink got = feed( stream, chunk );
        if( got.joined() != "hello|world" )
            return -1 - (int) chunk;
        if( got.dones != 0 )
            return -100 - (int) chunk;
    }
    return 0;
}

int test_sse_crlf_and_spacing()
{
    // `data:x`, `data: x` and CRLF endings are all in the wild; two spaces are
    // payload, not separator.
    PayloadSink got = feed( "data:x\r\ndata: y\r\n\r\ndata:  z\r\n", 4096 );
    if( got.joined() != "x|y| z" )
        return -1;
    return 0;
}

int test_sse_done_stops_the_stream()
{
    const PayloadSink got = feed( "data: a\n\ndata: [DONE]\ndata: b\n\n", 4096 );
    if( got.joined() != "a" )
        return -1;
    if( got.dones != 1 )
        return -2;
    return 0;
}

int test_sse_ignores_comments_and_keepalives()
{
    // A comment line from a proxy, and an empty `data:` whose only job is to keep
    // the connection warm.
    const PayloadSink got = feed( ": ping\n\ndata: \ndata: x\n\n", 4096 );
    if( got.joined() != "x" )
        return -1;
    return 0;
}

// --- ChatRequest ----------------------------------------------------------------

struct Recorder : public ChatRequest::Sink
{
    std::string text;
    std::string thinking;
    int finishes;
    bool ok;
    std::string error;
    std::vector<std::string> order;

    Recorder() : finishes( 0 ), ok( false ) {}

    virtual void onText( const std::string &piece, bool isThinking )
    {
        order.push_back( isThinking ? "t" : "c" );
        if( isThinking )
            thinking += piece;
        else
            text += piece;
    }

    virtual void onFinished( bool isOk, const std::string &why )
    {
        ++finishes;
        ok = isOk;
        error = why;
    }

    static bool drive( ChatRequest &request, int limitMs = 8000 )
    {
        const std::chrono::steady_clock::time_point end =
            std::chrono::steady_clock::now() + std::chrono::milliseconds( limitMs );
        while( request.busy() && std::chrono::steady_clock::now() < end )
            request.step( 10 );
        return !request.busy();
    }
};

bool start( ChatRequest &request, atui_test::ScriptedHttp &server,
            Recorder &sink, const std::string &model = "test-model",
            const std::string &content = "hi" )
{
    OaiRequest req;
    req.model = model;
    req.stream = true;
    OaiMessage m;
    m.role = "user";
    m.content = content;
    req.messages.push_back( m );

    const std::string base = "http://127.0.0.1:" + std::to_string( server.port() ) + "/v1";
    std::string error;
    return request.begin( base, "secret-key", req, &sink, 5, 0, &error );
}

int test_chat_streams_content()
{
    atui_test::ScriptedHttp server;
    if( !server.start() )
        return -1;
    server.enqueueResponse( sseResponse( sseBody( "hello there" ) ) );

    Recorder sink;
    ChatRequest request;
    if( !start( request, server, sink ) )
        return -2;
    if( !Recorder::drive( request ) )
        return -3;
    if( sink.text != "hello there" )
        return -4;
    if( sink.finishes != 1 || !sink.ok )
        return -5;
    if( request.response().finishReason != "stop" )
        return -6;
    return 0;
}

// The request is the part a wrong setting shows up in, so check the bytes: the path
// under the base URL, the bearer token, and the body the model is named in.
int test_chat_request_on_the_wire()
{
    atui_test::ScriptedHttp server;
    if( !server.start() )
        return -1;
    server.enqueueResponse( sseResponse( sseBody( "x" ) ) );

    Recorder sink;
    ChatRequest request;
    if( !start( request, server, sink, "my-model" ) )
        return -2;
    if( !Recorder::drive( request ) )
        return -3;

    const std::string sent = server.lastRequest();
    if( sent.find( "POST /v1/chat/completions HTTP/1.1\r\n" ) != 0 )
        return -4;
    if( sent.find( "Authorization: Bearer secret-key\r\n" ) == std::string::npos )
        return -5;
    if( sent.find( "Accept: text/event-stream\r\n" ) == std::string::npos )
        return -6;
    if( sent.find( "\"model\":\"my-model\"" ) == std::string::npos )
        return -7;
    if( sent.find( "\"content\":\"hi\"" ) == std::string::npos )
        return -8;
    return 0;
}

// Control characters in the body have to arrive escaped, and this asserts on the
// bytes the peer received rather than on a string built beside them.
//
// Not hypothetical, twice over: the OS/2 target's terminal hands the input line its
// escape sequences as text (a Home key arrives as characters), and a tool's output is
// routinely full of colour escapes.  Raw, the body is not JSON, and a real llama.cpp
// endpoint answers
//     500 {"message":"[json.exception.parse_error.101] ... invalid string:
//          control character U+001B (ESC) must be escaped"}
// which every layer above faithfully reports as a server problem -- true, and
// useless, because the encoder is what failed.  RFC 8259 wants every code point below
// U+0020 escaped; the six with short forms keep them.
int test_chat_escapes_control_characters_in_the_body()
{
    atui_test::ScriptedHttp server;
    if( !server.start() )
        return -1;
    server.enqueueResponse( sseResponse( sseBody( "x" ) ) );

    Recorder sink;
    ChatRequest request;
    const std::string awkward = std::string( "a" ) + char( 0x1B ) + char( 0x01 )
                              + char( 0x09 ) + char( 0x07 ) + char( 0x0C )
                              + char( 0x0A ) + char( 0x22 ) + "b";
    if( !start( request, server, sink, "m", awkward ) )
        return -2;
    if( !Recorder::drive( request ) )
        return -3;

    const std::string sent = server.lastRequest();
    // What the body must contain. The seven characters JSON gives short forms to keep
    // them -- this is not a hex-everything encoder -- and everything else below 0x20
    // arrives as \u00XX. Spelled out rather than inline because a literal of that
    // shape in C++ is a universal character name, and the compiler rightly refuses
    // a control character in the source when the point of the test is that byte in a
    // request body.
    const std::string want = "a\\u001b\\u0001\\t\\u0007\\f\\n\\\"b";
    if( sent.find( want ) == std::string::npos )
        return -4;

    // The stronger statement, and the one the server actually checks: after the
    // headers the body is one line, so no control byte at all may appear in it.
    const std::size_t bodyAt = sent.find( "\r\n\r\n" );
    if( bodyAt == std::string::npos )
        return -5;
    for( std::size_t i = bodyAt + 4; i < sent.size(); ++i )
    {
        const unsigned char c = static_cast< unsigned char >( sent[ i ] );
        if( c < 0x20 )
            return -6;
    }
    return 0;
}

// A request that is decided and a connection that is alive are not the same event, and
// only a machine that reaches the first step late can tell the difference.  Measured on
// the OS/2 box: the interface asked the model a question and first stepped the answer six
// seconds later -- it was drawing its first screen down a telnet link -- and the server
// hung up on a connection that had been opened and then left saying nothing.  "request
// failed: the server closed the connection without answering" was all the user got, for a
// fault that was ours and six seconds old.
//
// So the connect belongs to the first step, and this asserts both halves: that opening is
// deferred (`connections()` says so; the request bytes look the same either way, which is
// why the counter exists), and that stepping late then works like stepping at once.  Who
// steps this machine, and when, is not supposed to be part of it -- on the wire as well
// as in the code.
int test_chat_a_stepper_that_arrives_late_still_gets_the_answer()
{
    atui_test::ScriptedHttp server;
    if( !server.start() )
        return -1;
    server.enqueueResponse( sseResponse( sseBody( "hello" ) ) );

    Recorder sink;
    ChatRequest request;
    if( !start( request, server, sink, "m" ) )
        return -2;

    // Wait first, then look.  Without the pause a connection made here would usually not
    // have reached the accept loop yet, and zero would mean "not noticed" rather than
    // "none was opened" -- which is a test that passes by accident and would have passed
    // against the code it was written to catch.  Two hundred ms on a loopback is a
    // connect and an accept many times over, and still short beside the six seconds the
    // interface actually took; it is the late stepper as well as the quiet wire.
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    if( server.connections() != 0 )
        return -3;

    if( !Recorder::drive( request ) )
        return -4;
    if( server.connections() != 1 )
        return -5;
    if( !sink.ok || sink.text != "hello" )
        return -6;
    return 0;
}

int test_chat_keeps_reasoning_apart()
{
    atui_test::ScriptedHttp server;
    if( !server.start() )
        return -1;
    const std::string body =
        atui_test::deltaJson( "reasoning_content", "thinking hard" ) +
        atui_test::deltaJson( "content", "the answer" ) +
        "data: [DONE]\n\n";
    server.enqueueResponse( sseResponse( body ) );

    Recorder sink;
    ChatRequest request;
    if( !start( request, server, sink ) )
        return -2;
    if( !Recorder::drive( request ) )
        return -3;
    if( sink.thinking != "thinking hard" )
        return -4;
    if( sink.text != "the answer" )
        return -5;
    // Interleaving is what the UI folds into collapsible blocks, so the order the
    // sink heard it in matters as much as the split.
    if( sink.order.size() != 2 || sink.order[0] != "t" || sink.order[1] != "c" )
        return -6;
    if( request.response().content != "the answer" )
        return -7;
    return 0;
}

int test_chat_merges_tool_call_fragments()
{
    atui_test::ScriptedHttp server;
    if( !server.start() )
        return -1;
    // `id` and the name in one payload, the arguments split across two more -- the
    // shape every server streams tool calls in.
    const std::string body =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
        "\"id\":\"call_1\",\"function\":{\"name\":\"run_command\",\"arguments\":"
        "\"{\\\"command\\\":\\\"mk\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
        "\"function\":{\"arguments\":\"dir\\\"}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n";
    server.enqueueResponse( sseResponse( body ) );

    Recorder sink;
    ChatRequest request;
    if( !start( request, server, sink ) )
        return -2;
    if( !Recorder::drive( request ) )
        return -3;
    if( request.response().toolCalls.size() != 1 )
        return -4;
    if( request.response().toolCalls[0].id != "call_1" )
        return -5;
    if( request.response().toolCalls[0].name != "run_command" )
        return -6;
    if( request.response().toolCalls[0].arguments != "{\"command\":\"mkdir\"}" )
        return -7;
    if( request.response().finishReason != "tool_calls" )
        return -8;
    return 0;
}

// A status that is not 2xx has to arrive as an error carrying the server's words:
// the curl path ignored the status, and an expired key looked like an empty answer.
int test_chat_error_status_carries_the_body()
{
    atui_test::ScriptedHttp server;
    if( !server.start() )
        return -1;
    const std::string why = "{\"error\":\"bad credentials\"}";
    server.enqueueResponse(
        "HTTP/1.1 401 Unauthorized\r\nContent-Length: " +
        std::to_string( why.size() ) + "\r\n\r\n" + why );

    Recorder sink;
    ChatRequest request;
    if( !start( request, server, sink ) )
        return -2;
    if( !Recorder::drive( request ) )
        return -3;
    if( sink.finishes != 1 || sink.ok )
        return -4;
    if( sink.error.find( "401" ) == std::string::npos )
        return -5;
    if( sink.error.find( "bad credentials" ) == std::string::npos )
        return -6;
    if( !sink.text.empty() )
        return -7;               // an error body is not the model's answer
    return 0;
}

// Refused and badly framed at once: the status is the useful half, so it leads.
int test_chat_error_status_outranks_a_broken_framing()
{
    atui_test::ScriptedHttp server;
    if( !server.start() )
        return -1;
    server.enqueueResponse(
        "HTTP/1.1 403 Forbidden\r\nContent-Length: 900\r\n\r\nnot" );

    Recorder sink;
    ChatRequest request;
    if( !start( request, server, sink ) )
        return -2;
    if( !Recorder::drive( request ) )
        return -3;
    if( sink.finishes != 1 || sink.ok )
        return -4;
    if( sink.error.find( "403" ) == std::string::npos )
        return -5;
    return 0;
}

int test_chat_truncated_stream_is_an_error()
{
    atui_test::ScriptedHttp server;
    if( !server.start() )
        return -1;
    // Promises a chunk bigger than it sends, then hangs up.
    server.enqueueResponse(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "400\r\ndata: half an answer\n\n" );

    Recorder sink;
    ChatRequest request;
    if( !start( request, server, sink ) )
        return -2;
    if( !Recorder::drive( request ) )
        return -3;
    if( sink.finishes != 1 || sink.ok )
        return -4;
    return 0;
}

// `[DONE]` means the answer is over: a server that keeps the socket open after it
// must not hold up the transcript.
int test_chat_done_ends_it_without_the_close()
{
    atui_test::ScriptedHttp server;
    if( !server.start() )
        return -1;
    atui_test::ScriptedHttp::Script script;
    script.pieces.push_back( sseResponse( sseBody( "finished" ) ) );
    script.closeAfter = false;      // the connection stays open on purpose
    server.enqueue( script );

    Recorder sink;
    ChatRequest request;
    if( !start( request, server, sink ) )
        return -2;
    if( !Recorder::drive( request, 3000 ) )
        return -3;                 // would time out if [DONE] did not end it
    if( sink.text != "finished" )
        return -4;
    if( sink.finishes != 1 || !sink.ok )
        return -5;
    return 0;
}

// A server that hangs up while a request is going out.  Writing to that socket raises
// SIGPIPE on a POSIX host, whose default disposition is to terminate the process, so
// the failure of this test is not a red line in a summary -- the whole test binary
// dies.  Control-checked by taking the protection out of `sock_posix.cpp`: exit 141
// (128+13) in 5 runs out of 5, and 0/5 with it.  The same thing measured outside the
// suite is in `tools/probe_sigpipe.cpp`.
//
// The two details that make it reproduce are load-bearing.  The body has to be larger
// than the socket buffers -- at 200 KB everything is written before the peer has even
// decided to hang up, and the request then fails cleanly on read -- and the hangup
// has to come *after* the connection is accepted rather than before the first write,
// because a socket that already knows the peer is gone answers `EPIPE` without the
// signal.  A version of this test that got either wrong passed with the bug present.
int test_chat_a_peer_that_hangs_up_mid_send_is_an_error()
{
    atui_test::ScriptedHttp server;
    if( !server.start() )
        return -1;
    atui_test::ScriptedHttp::Script script;
    // Close while the request is still going out -- closing *before* the first write
    // teaches the stack about the hangup too early and the write just fails cleanly,
    // which is not the case under test.
    script.hangUpAfterMs = 50;   // while the write below is still going
    server.enqueue( script );

    // Big enough that the request cannot go out in one write: the first chunk is
    // absorbed by the socket buffer and the second is the one that finds the peer
    // gone.
    OaiRequest req;
    req.model = "m";
    req.stream = true;
    OaiMessage m;
    m.role = "user";
    m.content = std::string( 8u * 1024u * 1024u, 'x' );
    req.messages.push_back( m );

    Recorder sink;
    ChatRequest request;
    const std::string base =
        "http://127.0.0.1:" + std::to_string( server.port() ) + "/v1";
    std::string error;
    if( !request.begin( base, "", req, &sink, 5, 0, &error ) )
        return -2;
    if( !Recorder::drive( request, 8000 ) )
        return -3;                 // must end: an error, but must end
    if( sink.finishes != 1 || sink.ok )
        return -4;
    if( sink.error.empty() )
        return -5;
    return 0;
}

int test_chat_bad_url_is_refused_before_connecting()
{
    atui_test::ScriptedHttp server;
    if( !server.start() )
        return -1;

    OaiRequest req;
    req.model = "m";
    Recorder sink;
    ChatRequest request;
    std::string error;
    if( request.begin( "https://host/v1", "", req, &sink, 5, 0, &error ) )
        return -1;
    if( error.find( "https" ) == std::string::npos )
        return -2;
    if( request.busy() )
        return -3;
    if( sink.finishes != 0 )
        return -4;            // nothing started, so nothing to finish
    return 0;
}

} // anonymous namespace

void init_chat_request_tests()
{
    register_test( "sse: one payload per data line", &test_sse_one_payload_per_line );
    register_test( "sse: split at every size", &test_sse_split_at_every_size );
    register_test( "sse: CRLF and spacing", &test_sse_crlf_and_spacing );
    register_test( "sse: done stops the stream", &test_sse_done_stops_the_stream );
    register_test( "sse: ignores comments and keepalives",
                   &test_sse_ignores_comments_and_keepalives );

    register_test( "chat: streams content", &test_chat_streams_content );
    register_test( "chat: request on the wire", &test_chat_request_on_the_wire );
    register_test( "chat: keeps reasoning apart", &test_chat_keeps_reasoning_apart );
    register_test( "chat: merges tool call fragments",
                   &test_chat_merges_tool_call_fragments );
    register_test( "chat: error status carries the body",
                   &test_chat_error_status_carries_the_body );
    register_test( "chat: error status outranks a broken framing",
                   &test_chat_error_status_outranks_a_broken_framing );
    register_test( "chat: truncated stream is an error",
                   &test_chat_truncated_stream_is_an_error );
    register_test( "chat: [DONE] ends it without the close",
                   &test_chat_done_ends_it_without_the_close );
    register_test( "chat: a peer that hangs up mid-send is an error",
                   &test_chat_a_peer_that_hangs_up_mid_send_is_an_error );
    register_test( "chat: a stepper that arrives late still gets the answer",
                   &test_chat_a_stepper_that_arrives_late_still_gets_the_answer );
    register_test( "chat: control characters are escaped in the body",
                   &test_chat_escapes_control_characters_in_the_body );
    register_test( "chat: bad url is refused before connecting",
                   &test_chat_bad_url_is_refused_before_connecting );
}

} // namespace atui_test

#endif // ATUI_HAVE_NETWORK
