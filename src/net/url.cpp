// Definitions for the endpoint splitter.  See url.h.
#include "url.h"

#include "core/strutil.h"

#include <stdlib.h>

namespace atui {
namespace net {

namespace {

bool isDigit( char c )
{
    return c >= '0' && c <= '9';
}

std::string lowerCase( const std::string &s )
{
    std::string out( s );
    for ( std::size_t i = 0; i < out.size(); ++i )
        if ( out[i] >= 'A' && out[i] <= 'Z' )
            out[i] = char ( out[i] - 'A' + 'a' );
    return out;
}

} // anonymous namespace

bool parseHttpUrl( const std::string &url, HttpEndpoint &out, std::string &reason )
{
    out = HttpEndpoint();
    reason.clear();

    const std::string text = url;
    if ( text.empty() )
    {
        reason = "no server URL set";
        return false;
    }

    std::string rest;
    if ( core::startsWith( text, "http://" ) )
        rest = text.substr( 7 );
    else if ( core::startsWith( text, "https://" ) )
    {
        // Recognised and stopped, rather than downgraded: the reason atui has its
        // own HTTP client is that there is no TLS library that compiles for it, and
        // an `https://` that quietly became `http://` would send an API key to a
        // LAN in plain text while looking like it had not.
        reason = "atui speaks plain HTTP: https is not supported (no TLS on this "
                 "platform). Use the http:// form of the server address";
        return false;
    }
    else
    {
        reason = "the server address needs http:// in front of it (for example "
                 "http://" + text + ")";
        return false;
    }

    // The path starts at the first slash; what is in front of it is host[:port].
    // A URL carrying a query string or a fragment is not what a base URL is, and
    // the '?' is left where it is and reported, because silently dropping it would
    // answer a different question from the one asked.
    std::string authority;
    std::string path;
    const std::size_t slash = rest.find( '/' );
    if ( slash == std::string::npos )
    {
        authority = rest;
        path = "/";
    }
    else
    {
        authority = rest.substr( 0, slash );
        path = rest.substr( slash );
    }

    if ( authority.find( '?' ) != std::string::npos ||
         authority.find( '#' ) != std::string::npos ||
         authority.find( '@' ) != std::string::npos )
    {
        reason = "the server address should be just host and port, for example "
                 "http://192.168.1.10:8080/v1";
        return false;
    }
    if ( authority.empty() )
    {
        reason = "no host in the server address";
        return false;
    }

    // The port is optional. Note that an IPv6 literal in brackets is not handled:
    // atui talks to a host on a LAN, and every real endpoint measured here is a
    // dotted quad, so the parser says so rather than carrying a grammar.
    const std::size_t colon = authority.rfind( ':' );
    if ( colon == std::string::npos )
    {
        out.host = lowerCase( authority );
        out.port = 80;
    }
    else
    {
        out.host = lowerCase( authority.substr( 0, colon ) );
        const std::string digits = authority.substr( colon + 1 );
        if ( digits.empty() || digits.size() > 5 )
        {
            reason = "the port in the server address is not a number";
            return false;
        }
        for ( std::size_t i = 0; i < digits.size(); ++i )
        {
            if ( !isDigit( digits[i] ) )
            {
                reason = "the port in the server address is not a number";
                return false;
            }
        }
        const long port = strtol( digits.c_str(), 0, 10 );
        if ( port < 1 || port > 65535 )
        {
            reason = "the port in the server address is out of range";
            return false;
        }
        out.port = int ( port );
    }

    if ( out.host.empty() )
    {
        reason = "no host in the server address";
        return false;
    }
    if ( path.find( '?' ) != std::string::npos || path.find( '#' ) != std::string::npos )
    {
        reason = "the server address is the base of the API, not a query";
        return false;
    }

    out.path = path;
    return true;
}

std::string hostHeader( const HttpEndpoint &endpoint )
{
    std::string out = endpoint.host;
    if ( endpoint.port != 80 )
    {
        out += ":";
        out += core::toString( endpoint.port );
    }
    return out;
}

std::string joinPath( const std::string &prefix, const std::string &path )
{
    if ( prefix.empty() || prefix == "/" )
        return "/" + path;
    if ( prefix[prefix.size() - 1] == '/' )
        return prefix + path;
    return prefix + "/" + path;
}

} // namespace atui::net
} // namespace atui
