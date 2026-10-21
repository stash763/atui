/* Does a refused connection crash on exit?
 *
 * Measured on the target with atui.exe --judge-check against a port nothing listens
 * on: the error is reported correctly and then the process dies with exception 0005
 * during teardown.  This is the same request with everything else removed -- no
 * provider settings, no permission engine, no judge, no TUI -- so that the answer
 * says which of the two it is: the socket layer closing something it should not, or
 * a static object in the layers above being destroyed in the wrong order.
 *
 * Built by `build.cmd netprobe probe_refuse.cpp prefuse.exe`, which links exactly
 * http.cpp, sock_os2.cpp and strutil.cpp with it: nothing else is in the image, so
 * nothing else can be the cause.
 */
#include "net/http.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define HOST "10.196.88.170"
#define PORT 9           /* discard: present in services, not listening here */

namespace {

class Printer : public atui::net::HttpStream::Sink
{
public:
    Printer() : finishes_( 0 ), ok_( false ), code_( 0 ) {}

    virtual void onStatus( int code ) { code_ = code; }
    virtual void onBody( const char *d, size_t n ) { body_.append( d, n ); }
    virtual void onFinish( bool ok, const std::string &why )
    {
        ++finishes_;
        ok_ = ok;
        printf( "  finish ok=%d reason=[%s] status=%d bytes=%d\n",
                (int) ok, why.c_str(), code_, (int) body_.size() );
        fflush( stdout );
    }

    int finishes_;
    bool ok_;
    int code_;
    std::string body_;
};

} // anonymous namespace

int main()
{
    printf( "probe_refuse: connecting to " HOST ":%d, which nothing listens on\n",
            PORT );
    fflush( stdout );

    Printer printer;
    atui::net::HttpStream stream;

    std::string why;
    // method, host, port, path, contentType, body, accept, authorization,
    // connectSeconds, idleSeconds, sink, reason  (http.h; the authorization argument
    // is the one that is easy to lose when writing this from memory).
    if( !stream.start( "GET", HOST, PORT, "/models", "", "", "application/json", "",
                       5, 0, &printer, &why ) )
    {
        printf( "  could not even start: [%s]\n", why.c_str() );
        fflush( stdout );
        printf( "probe_refuse: reached the end without a stream, exiting normally\n" );
        return 0;
    }

    const time_t end = time( 0 ) + 10;
    while( stream.busy() && time( 0 ) < end )
        stream.step( 100 );
    if( stream.busy() )
    {
        printf( "  still busy after 10s, aborting\n" );
        stream.abort( "probe timeout" );
    }

    printf( "probe_refuse: finishes=%d ok=%d -- now returning from main\n",
            printer.finishes_, (int) printer.ok_ );
    fflush( stdout );
    return 0;
}
