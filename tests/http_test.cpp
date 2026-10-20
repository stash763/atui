// Tests for the event-loop HTTP client, run against a scripted responder on
// localhost (http_pipe.h) -- so the awkward cases are the ones under test: headers
// split mid-word, chunk framing, a server that stops in the middle of an answer.
//
// Network-only: the body is empty when ATUI_HAVE_NETWORK is off, as the transport
// is not built at all in that configuration.
#include "harness.h"

#ifdef ATUI_HAVE_NETWORK

#include "http_pipe.h"
#include "net/http.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace atui_test {

namespace {

using atui::net::HttpStream;

// Everything the sink was told, in order.  A client that finishes twice, or delivers
// a body but never a status, is as wrong as one that fails, and only a record of the
// sequence says so.
struct Recorder : public HttpStream::Sink
{
    std::vector<int> statuses;
    std::string body;
    int finishes;
    bool ok;
    std::string reason;

    Recorder() : finishes( 0 ), ok( false ) {}

    void onStatus( int code ) { statuses.push_back( code ); }
    void onBody( const char *data, size_t len ) { body.append( data, len ); }
    void onFinish( bool isOk, const std::string &why )
    {
        ++finishes;
        ok = isOk;
        reason = why;
    }

    // Drives a stream to its end.  The step wait is short and the ceiling generous:
    // what is being checked is the answer, not the speed, and a test that times out
    // is a state machine that is waiting for something that will not arrive.
    static bool drive( HttpStream &stream, int limitMs = 8000 )
    {
        const std::chrono::steady_clock::time_point end =
            std::chrono::steady_clock::now() + std::chrono::milliseconds( limitMs );
        while( stream.busy() && std::chrono::steady_clock::now() < end )
            stream.step( 10 );
        return !stream.busy();
    }
};

// A chunk as the framing wants it: hex size, CRLF, bytes, CRLF.
std::string chunk( const std::string &payload )
{
    char head[ 32 ];
    std::snprintf( head, sizeof head, "%lx\r\n", (unsigned long) payload.size() );
    return std::string( head ) + payload + "\r\n";
}

const char *kBody = "hello from the scripted server";

std::string plainResponse( const std::string &body, const std::string &status = "200 OK" )
{
    char head[ 128 ];
    std::snprintf( head, sizeof head, "HTTP/1.1 %s\r\nContent-Length: %lu\r\n",
                  status.c_str(),
                  (unsigned long) body.size() );
    return std::string( head ) + "\r\n" + body;
}

// Starts a request against `server` and checks it got under way; almost every test
// begins this way, and a failure here is not the interesting kind.
bool startAt( HttpStream &stream, ScriptedHttp &server, Recorder &sink,
              const std::string &method = "POST",
              const std::string &path = "/v1/chat/completions",
              const std::string &body = kBody,
              const std::string &authorization = "Bearer test-key" )
{
    std::string why;
    return stream.start( method, "127.0.0.1", server.port(), path,
                         "application/json", body, "text/event-stream",
                         authorization, 5, 0, &sink, &why );
}

// --- the request ---------------------------------------------------------------

int test_request_on_the_wire()
{
    ScriptedHttp server;
    if( !server.start() )
        return -1;
    server.enqueueResponse( plainResponse( "ok" ) );

    Recorder sink;
    HttpStream stream;
    if( !startAt( stream, server, sink ) )
        return -2;
    if( !Recorder::drive( stream ) )
        return -3;

    const std::string sent = server.lastRequest();
    if( sent.find( "POST /v1/chat/completions HTTP/1.1\r\n" ) != 0 )
        return -4;
    if( sent.find( "Host: 127.0.0.1:" ) == std::string::npos )
        return -5;
    if( sent.find( "Authorization: Bearer test-key\r\n" ) == std::string::npos )
        return -6;
    if( sent.find( "Content-Type: application/json\r\n" ) == std::string::npos )
        return -7;
    if( sent.find( "Accept: text/event-stream\r\n" ) == std::string::npos )
        return -8;
    const std::string length = "Content-Length: " +
        std::to_string( std::string( kBody ).size() ) + "\r\n";
    if( sent.find( length ) == std::string::npos )
        return -9;
    if( sent.find( "Connection: close\r\n" ) == std::string::npos )
        return -10;
    if( sent.size() < strlen( kBody ) ||
        sent.compare( sent.size() - strlen( kBody ), strlen( kBody ), kBody ) != 0 )
        return -11;                      // the body has to be the last thing
    return 0;
}

// --- framing -------------------------------------------------------------------

int test_plain_body()
{
    ScriptedHttp server;
    if( !server.start() )
        return -1;
    server.enqueueResponse( plainResponse( kBody ) );

    Recorder sink;
    HttpStream stream;
    if( !startAt( stream, server, sink ) )
        return -2;
    if( !Recorder::drive( stream ) )
        return -3;
    if( sink.statuses.size() != 1 || sink.statuses[0] != 200 )
        return -4;
    if( sink.body != kBody )
        return -5;
    if( sink.finishes != 1 || !sink.ok )
        return -6;
    return 0;
}

int test_headers_split_across_reads()
{
    ScriptedHttp server;
    if( !server.start() )
        return -1;
    // Seven bytes at a time: the status line, every header and the blank line all
    // arrive in pieces, which is the shape a client that parses "one read, one
    // response" gets wrong.
    server.enqueueResponse( plainResponse( kBody ), 2, 7 );

    Recorder sink;
    HttpStream stream;
    if( !startAt( stream, server, sink ) )
        return -2;
    if( !Recorder::drive( stream ) )
        return -3;
    if( sink.statuses.size() != 1 || sink.statuses[0] != 200 )
        return -4;
    if( sink.body != kBody )
        return -5;
    if( sink.finishes != 1 || !sink.ok )
        return -6;
    return 0;
}

int test_body_split_across_reads()
{
    ScriptedHttp server;
    if( !server.start() )
        return -1;
    server.enqueueResponse( plainResponse( kBody ), 2, 3 );

    Recorder sink;
    HttpStream stream;
    if( !startAt( stream, server, sink ) )
        return -2;
    if( !Recorder::drive( stream ) )
        return -3;
    if( sink.body != kBody )
        return -5;
    if( sink.finishes != 1 || !sink.ok )
        return -6;
    return 0;
}

int test_chunked_body()
{
    ScriptedHttp server;
    if( !server.start() )
        return -1;
    std::string response =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" +
        chunk( "data: one\n\n" ) + chunk( "data: two\n\n" ) + "0\r\n\r\n";
    server.enqueueResponse( response );

    Recorder sink;
    HttpStream stream;
    if( !startAt( stream, server, sink ) )
        return -2;
    if( !Recorder::drive( stream ) )
        return -3;
    if( sink.body != "data: one\n\ndata: two\n\n" )
        return -4;
    if( sink.finishes != 1 || !sink.ok )
        return -5;
    return 0;
}

int test_chunked_one_byte_at_a_time()
{
    ScriptedHttp server;
    if( !server.start() )
        return -1;
    std::string response =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" +
        chunk( "data: aaa\n\n" ) + chunk( "data: bbb\n\n" ) + "0\r\n\r\n";
    server.enqueueResponse( response, 0, 1 );

    Recorder sink;
    HttpStream stream;
    if( !startAt( stream, server, sink ) )
        return -2;
    if( !Recorder::drive( stream ) )
        return -3;
    if( sink.body != "data: aaa\n\ndata: bbb\n\n" )
        return -4;
    if( sink.finishes != 1 || !sink.ok )
        return -5;
    return 0;
}

// A body that ends at the close, with no length and no chunks: an error page from a
// server that just answers and hangs up.
int test_body_until_close()
{
    ScriptedHttp server;
    if( !server.start() )
        return -1;
    server.enqueueResponse( "HTTP/1.1 500 Server Error\r\n\r\nsomething broke" );

    Recorder sink;
    HttpStream stream;
    if( !startAt( stream, server, sink ) )
        return -2;
    if( !Recorder::drive( stream ) )
        return -3;
    if( sink.statuses.size() != 1 || sink.statuses[0] != 500 )
        return -4;
    if( sink.body != "something broke" )
        return -5;
    if( sink.finishes != 1 || !sink.ok )
        return -6;
    return 0;
}

int test_empty_body()
{
    ScriptedHttp server;
    if( !server.start() )
        return -1;
    server.enqueueResponse( plainResponse( "" ) );

    Recorder sink;
    HttpStream stream;
    if( !startAt( stream, server, sink ) )
        return -2;
    if( !Recorder::drive( stream ) )
        return -3;
    if( !sink.body.empty() )
        return -4;
    if( sink.finishes != 1 || !sink.ok )
        return -5;
    return 0;
}

// --- what is not an answer -----------------------------------------------------

// The status code is the server's to report and the caller's to interpret: the
// transport's job is to say "415, and here is the body that explains it", not to
// decide whether that is an error.
int test_error_status_is_delivered()
{
    ScriptedHttp server;
    if( !server.start() )
        return -1;
    server.enqueueResponse(
        plainResponse( "no media type for you", "415 Unsupported Media Type" ) );

    Recorder sink;
    HttpStream stream;
    if( !startAt( stream, server, sink ) )
        return -2;
    if( !Recorder::drive( stream ) )
        return -3;
    if( sink.statuses.size() != 1 || sink.statuses[0] != 415 )
        return -4;
    if( sink.body != "no media type for you" )
        return -5;
    if( sink.finishes != 1 || !sink.ok )
        return -6;
    return 0;
}

// A connection closed in the middle of a promised body is the failure most likely to
// be mistaken for a short answer, and it is the one a streamed chat can produce when
// a server runs out of memory mid-generation.
int test_truncated_body_is_an_error()
{
    ScriptedHttp server;
    if( !server.start() )
        return -1;
    const std::string body( 2000, 'x' );
    server.enqueueTruncated( plainResponse( body ).substr( 0, 60 ), "" );

    Recorder sink;
    HttpStream stream;
    if( !startAt( stream, server, sink ) )
        return -2;
    if( !Recorder::drive( stream ) )
        return -3;
    if( sink.finishes != 1 )
        return -4;
    if( sink.ok )
        return -5;
    if( sink.reason.empty() )
        return -6;
    return 0;
}

int test_close_without_answering()
{
    ScriptedHttp server;
    if( !server.start() )
        return -1;
    ScriptedHttp::Script silence;      // no pieces at all: accept and close
    server.enqueue( silence );

    Recorder sink;
    HttpStream stream;
    if( !startAt( stream, server, sink ) )
        return -2;
    if( !Recorder::drive( stream ) )
        return -3;
    if( sink.finishes != 1 || sink.ok )
        return -4;
    if( !sink.statuses.empty() )
        return -5;
    return 0;
}

int test_not_http()
{
    ScriptedHttp server;
    if( !server.start() )
        return -1;
    server.enqueueResponse( "SMTP 220 I am not a web server\r\n" );

    Recorder sink;
    HttpStream stream;
    if( !startAt( stream, server, sink ) )
        return -2;
    if( !Recorder::drive( stream ) )
        return -3;
    if( sink.finishes != 1 || sink.ok )
        return -4;
    return 0;
}

// Nothing listening on that port.  Where this is reported differs by platform -- the
// BSD path fails in `start()`, the OS/2 path only once select() and SO_ERROR have
// been asked, since connect() there says nothing either way -- so the test accepts
// either, and insists only that the failure is a failure and has words.
int test_connection_refused()
{
    ScriptedHttp server;
    if( !server.start() )
        return -1;
    const int open = server.port();
    server.stop();                   // whatever port it had is now nobody's

    Recorder sink;
    HttpStream stream;
    std::string why;
    const bool started = stream.start( "GET", "127.0.0.1", open, "/", "", "", "",
                                       "", 5, 0, &sink, &why );
    if( !started )
        return why.empty() ? -2 : 0;

    if( !Recorder::drive( stream ) )
        return -3;
    if( sink.finishes != 1 || sink.ok )
        return -4;
    if( sink.reason.empty() )
        return -5;
    return 0;
}

// A request can be given up on (escape pressed), and giving up has to look like
// every other ending to the caller: one finish, with a reason.
int test_abort_ends_it_once()
{
    ScriptedHttp server;
    if( !server.start() )
        return -1;
    ScriptedHttp::Script never;
    never.closeAfter = false;        // connected, and then says nothing
    server.enqueue( never );

    Recorder sink;
    HttpStream stream;
    if( !startAt( stream, server, sink ) )
        return -2;
    stream.step( 50 );
    if( !stream.busy() )
        return -3;
    stream.abort( "cancelled by the user" );
    if( stream.busy() )
        return -4;
    if( sink.finishes != 1 || sink.ok )
        return -5;
    if( sink.reason != "cancelled by the user" )
        return -6;
    // And ending twice is not two finishes.
    stream.abort( "again" );
    if( sink.finishes != 1 )
        return -7;
    return 0;
}

} // anonymous namespace

void init_http_transport_tests()
{
    register_test( "http: request on the wire", &test_request_on_the_wire );
    register_test( "http: plain body", &test_plain_body );
    register_test( "http: headers split across reads",
                   &test_headers_split_across_reads );
    register_test( "http: body split across reads", &test_body_split_across_reads );
    register_test( "http: chunked body", &test_chunked_body );
    register_test( "http: chunked one byte at a time",
                   &test_chunked_one_byte_at_a_time );
    register_test( "http: body until close", &test_body_until_close );
    register_test( "http: empty body", &test_empty_body );
    register_test( "http: error status is delivered", &test_error_status_is_delivered );
    register_test( "http: truncated body is an error",
                   &test_truncated_body_is_an_error );
    register_test( "http: close without answering", &test_close_without_answering );
    register_test( "http: not http", &test_not_http );
    register_test( "http: connection refused", &test_connection_refused );
    register_test( "http: abort ends it once", &test_abort_ends_it_once );
}

} // namespace atui_test

#endif // ATUI_HAVE_NETWORK
