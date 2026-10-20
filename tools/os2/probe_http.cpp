/* probe_http.cpp -- the HTTP state machine, on the target, against the real server.
 *
 * Development probe, not part of the program:
 *
 *     build.cmd netprobe probe_http.cpp probe_http.exe
 *
 * `http.cpp` and `sock_os2.cpp` compile on the host and pass 14 tests there, which
 * says the state machine is right and says nothing about whether this platform is
 * the one it was written for.  Three things can only be answered on the box:
 *
 *   1. do the two files compile with Watcom at all (the include dance in
 *      sock_os2.cpp, and nothing in `http.cpp` assuming a newer library);
 *   2. does the state machine reach a real server through `soclose`/`sock_errno`/
 *      `FIONBIO` rather than through the host's socket calls -- in particular, does
 *      a connect that returns -1 while succeeding still end in a response;
 *   3. does the chunked framing produced by llama.cpp's streaming answer parse the
 *      same way it did in the mock, where the answer arrived in pieces of a few
 *      hundred bytes and not in the tidy ones a test writes.
 *
 * So it asks the endpoint three questions in a row, on three separate connections,
 * which is also the leak check: a socket that was never really closed shows up as
 * the third request failing to open one.  The body of each answer is printed as it
 * arrives, because "it streamed" and "it arrived after the end" are different and
 * only the timing on the screen tells them apart.
 */
#include "net/http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HOST "10.196.88.170"
#define PORT 10001

using atui::net::HttpStream;

/* Prints what arrives as it arrives.  The counters are what the test asserts on;
   the printing is so that a human reading the log can see the shape of the stream
   rather than only its total. */
class Printer : public HttpStream::Sink
{
public:
    explicit Printer( const char *label )
        : label_( label ), status_( 0 ), bytes_( 0), pieces_( 0 ),
          finishes_( 0 ), ok_( false )
    {
    }

    virtual void onStatus( int code )
    {
        status_ = code;
        printf( "  %s: status %d\n", label_, code );
    }

    virtual void onBody( const char *data, size_t len )
    {
        ++pieces_;
        bytes_ += (int) len;
        if( pieces_ <= 3 )
        {
            int show = (int) len;
            if( show > 70 ) show = 70;
            printf( "  %s: piece %d, %d bytes: [%.*s]%s\n", label_, pieces_,
                    (int) len, show, data, len > (size_t) show ? "..." : "" );
        }
    }

    virtual void onFinish( bool ok, const std::string &why )
    {
        ++finishes_;
        ok_ = ok;
        printf( "  %s: finish ok=%d reason=%s (%d pieces, %d bytes)\n",
                label_, (int) ok, why.empty() ? "-" : why.c_str(), pieces_, bytes_ );
    }

    int status_;
    int bytes_;
    int pieces_;
    int finishes_;
    bool ok_;

private:
    const char *label_;
};

/* Drives a stream until it ends or the clock says enough. */
static void run( HttpStream &stream, int seconds )
{
    const time_t end = time( 0 ) + seconds;
    while( stream.busy() && time( 0 ) < end )
        stream.step( 100 );
    if( stream.busy() )
    {
        printf( "  still running after %d seconds -- giving up on it\n", seconds );
        stream.abort( "probe timeout" );
    }
}

/* The first `"id":"..."` in a /models answer.  atui has a JSON extractor for this
   and this probe deliberately does not pull it in: three lines of searching is a
   smaller thing to be wrong about. */
static std::string firstModel( const std::string &body )
{
    const char *key = "\"id\":\"";
    const size_t at = body.find( key );
    if( at == std::string::npos )
        return std::string();
    const size_t from = at + strlen( key );
    const size_t to = body.find( '"', from );
    if( to == std::string::npos )
        return std::string();
    return body.substr( from, to - from );
}

int main( void )
{
    /* Unbuffered: how this gets read is over a redirect into a file fetched while
       the program is still running (see the other probes). */
    setbuf( stdout, 0 );
    printf( "probe_http: the event-loop transport against " HOST ":%d\n", PORT );

    std::string model;      // whatever the endpoint says it serves, used below

    /* --- 1. a GET, the smallest thing that can go wrong --------------------- */
    {
        std::string models;
        class Getter : public HttpStream::Sink
        {
        public:
            Getter( std::string &out ) : body_( out ), finishes_( 0 ), ok_( false ) {}
            virtual void onStatus( int code ) { printf( "  models: status %d\n", code ); }
            virtual void onBody( const char *d, size_t n ) { body_.append( d, n ); }
            virtual void onFinish( bool ok, const std::string &why )
            {
                ++finishes_;
                ok_ = ok;
                printf( "  models: finish ok=%d reason=%s, %d bytes\n",
                        (int) ok, why.empty() ? "-" : why.c_str(), (int) body_.size() );
            }
            std::string &body_;
            int finishes_;
            bool ok_;
        };

        HttpStream list;
        Getter getter( models );
        std::string why;
        if( !list.start( "GET", HOST, PORT, "/models", "", "", "application/json",
                         "", 15, 0, &getter, &why ) )
        {
            printf( "models: could not start: %s\n", why.c_str() );
            return 1;
        }
        run( list, 30 );
        printf( "  first 200 bytes: [%.200s]\n", models.c_str() );
        printf( "  one finish? %s\n", getter.finishes_ == 1 ? "yes" : "NO" );
        model = firstModel( models );
        printf( "  model to ask for: %s\n", model.empty() ? "(none found)" : model.c_str() );
    }

    /* --- 2. a streamed POST, which is the request atui actually makes -------- */
    for( int attempt = 1; attempt <= 2; ++attempt )
    {
        char label[ 32 ];
        sprintf( label, "chat #%d", attempt );

        const std::string body =
            std::string( "{\"model\":\"" ) + ( model.empty() ? "probe" : model ) +
            "\",\"messages\":[{\"role\":\"user\",\"content\":\"Reply with exactly"
            " TRANSPORT-OK\"}],\"stream\":true,\"max_tokens\":16}";

        HttpStream stream;
        Printer sink( label );
        std::string why;
        if( !stream.start( "POST", HOST, PORT, "/v1/chat/completions",
                           "application/json", body, "text/event-stream", "",
                           15, 0, &sink, &why ) )
        {
            printf( "%s: could not start: %s\n", label, why.c_str() );
            return 2;
        }
        run( stream, 90 );
        printf( "%s: status=%d pieces=%d bytes=%d finished=%d ok=%d\n",
                label, sink.status_, sink.pieces_, sink.bytes_, sink.finishes_,
                (int) sink.ok_ );
    }

    printf( "\ndone\n" );
    return 0;
}
