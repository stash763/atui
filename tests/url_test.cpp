// Tests for the endpoint splitter: the four words of a base URL, and the ways a
// settings file can spell it wrong.
//
// These are worth having because a wrongly-parsed address does not fail loudly --
// it connects somewhere else, or to the right machine on the wrong port, and the
// answer arrives from a place the user did not mean.  Every case below is a string
// somebody could reasonably type into the Providers dialog.
#include "harness.h"
#include "net/url.h"

#include <string>

namespace atui_test {

namespace {

using atui::net::HttpEndpoint;

// Parses and returns the endpoint, or a negative code naming the failure.
int parse( const std::string &url, HttpEndpoint &out )
{
    std::string reason;
    if( !atui::net::parseHttpUrl( url, out, reason ) )
        return -100;
    return 0;
}

int test_host_port_path()
{
    HttpEndpoint e;
    if( parse( "http://192.168.1.10:8080/v1", e ) != 0 )
        return -1;
    if( e.host != "192.168.1.10" )
        return -2;
    if( e.port != 8080 )
        return -3;
    if( e.path != "/v1" )
        return -4;
    return 0;
}

int test_default_port_and_no_path()
{
    HttpEndpoint e;
    if( parse( "http://inference.lan", e ) != 0 )
        return -1;
    if( e.port != 80 )
        return -2;
    if( e.path != "/" )
        return -3;
    return 0;
}

int test_trailing_slash_is_kept()
{
    HttpEndpoint e;
    if( parse( "http://host:8080/v1/", e ) != 0 )
        return -1;
    if( e.path != "/v1/" )
        return -2;
    // ...and joining must not turn that into `//chat/completions`.
    if( atui::net::joinPath( e.path, "chat/completions" ) != "/v1/chat/completions" )
        return -3;
    return 0;
}

int test_host_is_lowercased()
{
    HttpEndpoint e;
    if( parse( "http://Inference.LAN:9/V1", e ) != 0 )
        return -1;
    if( e.host != "inference.lan" )
        return -2;
    return 0;
}

int test_https_is_refused()
{
    // Not "silently made http": the whole reason this client is hand-written is
    // that there is no TLS here, so an https that went out in clear text with an
    // API key in a header would look like it had worked.
    HttpEndpoint e;
    std::string reason;
    if( atui::net::parseHttpUrl( "https://host/v1", e, reason ) )
        return -1;
    if( reason.find( "https" ) == std::string::npos )
        return -2;
    return 0;
}

int test_missing_scheme_is_refused_with_an_example()
{
    HttpEndpoint e;
    std::string reason;
    if( atui::net::parseHttpUrl( "localhost:8080/v1", e, reason ) )
        return -1;
    // The message's job is to be typed at: it should contain the address corrected.
    if( reason.find( "http://localhost:8080/v1" ) == std::string::npos )
        return -2;
    return 0;
}

int test_bad_ports_are_refused()
{
    HttpEndpoint e;
    std::string reason;
    const char *bad[] = { "http://host:0/v1", "http://host:99999/v1",
                          "http://host:abc/v1", "http://host:/v1",
                          "http://host:80x/v1" };
    for( std::size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i )
    {
        if( atui::net::parseHttpUrl( bad[i], e, reason ) )
            return -1 - (int) i;
        if( reason.empty() )
            return -10 - (int) i;      // refused, but with nothing to say
    }
    return 0;
}

int test_what_is_not_a_base_url_is_refused()
{
    HttpEndpoint e;
    std::string reason;
    const char *bad[] = { "", "http:///v1", "http://user@host/v1",
                          "http://host:8080/v1?model=x", "HTTPEXTRA" };
    for( std::size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i )
    {
        if( atui::net::parseHttpUrl( bad[i], e, reason ) )
            return -1 - (int) i;
    }
    return 0;
}

int test_host_header()
{
    HttpEndpoint e;
    if( parse( "http://host:8080/v1", e ) != 0 )
        return -1;
    if( atui::net::hostHeader( e ) != "host:8080" )
        return -2;
    if( parse( "http://host/v1", e ) != 0 )
        return -3;
    // Port 80 is the default and a `Host:` carrying it is noise a server tolerates.
    if( atui::net::hostHeader( e ) != "host" )
        return -4;
    return 0;
}

int test_join_path()
{
    if( atui::net::joinPath( "/v1", "chat/completions" ) != "/v1/chat/completions" )
        return -1;
    if( atui::net::joinPath( "", "models" ) != "/models" )
        return -2;
    if( atui::net::joinPath( "/", "models" ) != "/models" )
        return -3;
    if( atui::net::joinPath( "/v1/", "models" ) != "/v1/models" )
        return -4;
    return 0;
}

} // anonymous namespace

void init_url_tests()
{
    register_test( "url: host, port and path", &test_host_port_path );
    register_test( "url: default port, no path", &test_default_port_and_no_path );
    register_test( "url: trailing slash is kept", &test_trailing_slash_is_kept );
    register_test( "url: host is lowercased", &test_host_is_lowercased );
    register_test( "url: https is refused", &test_https_is_refused );
    register_test( "url: missing scheme is refused with an example",
                   &test_missing_scheme_is_refused_with_an_example );
    register_test( "url: bad ports are refused", &test_bad_ports_are_refused );
    register_test( "url: what is not a base url is refused",
                   &test_what_is_not_a_base_url_is_refused );
    register_test( "url: Host header", &test_host_header );
    register_test( "url: join path", &test_join_path );
}

} // namespace atui_test
