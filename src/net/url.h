// The four words of an endpoint: scheme, host, port, path.
//
// atui is given a base URL in the shape a llama.cpp or Ollama server is described:
// `http://192.168.1.10:8080/v1`.  Everything atui sends is `<base>/<something>`, so
// the four parts have to come apart before a socket can be opened at them, and the
// cases that are not that shape have to be refused with a sentence rather than
// produce a connection to the wrong place.
//
// Small as it is, this is its own file because two things read it: the event-loop
// transport, and the settings dialog that has to say "that is not a URL" while the
// user is still typing.
//
// HTTPS is recognised and refused.  Not because encryption is unwanted -- it is the
// reason there is a hand-written HTTP client at all, since no TLS library in this
// tree compiles for Open Watcom -- but a request that silently went out in clear
// text because a scheme was ignored would be the worst possible failure of it.
#ifndef ATUI_NET_URL_H
#define ATUI_NET_URL_H

#include <string>

namespace atui {
namespace net {

struct HttpEndpoint
{
    HttpEndpoint() : port( 80 ), secure( false ) {}

    std::string host;
    int port;
    std::string path;     // leading slash, "" becomes "/"
    bool secure;          // https: -- recognised, and refused before connecting
};

// Takes `url` apart.  Returns false with a reason a person can act on when it is
// not `http://host[:port][/path]` (or the https form, which says so).  A missing
// scheme is treated as a mistake rather than guessed at: a settings file that says
// `localhost:8080/v1` means http, and saying so in the error is one keystroke for
// the user and an avoided hour for whoever debugs a connection to port 80.
bool parseHttpUrl( const std::string &url, HttpEndpoint &out, std::string &reason );

// The value of a `Host:` header for this endpoint: `:port` only when it is not 80.
std::string hostHeader( const HttpEndpoint &endpoint );

// Joins the endpoint's path prefix with a request path: `/v1` + `chat/completions`
// is `/v1/chat/completions`, and a prefix that is empty or `/` does not double up.
std::string joinPath( const std::string &prefix, const std::string &path );

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_URL_H
