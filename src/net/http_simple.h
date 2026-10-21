// The few things that need HTTP but have no event loop to step one.
//
// atui's chat path is a state machine driven from the TUI, and that is the right
// shape: the screen keeps drawing while a model thinks.  But three callers are not
// inside the TUI -- the model list the Providers dialog asks for, the permission
// agent's question from `--judge-check`, and the corpus mode of that same tool -- and
// for those the honest thing is a blocking call that says so at its door rather than
// a second transport.  Each one is a `HttpStream` (or a `ChatRequest`) pumped in a
// loop by whoever is waiting, which is the same code the event loop runs, minus the
// event loop.
//
// C++98, no threads: the pump is the reason there are no threads here, and the
// reason the chat path cannot use it.
#ifndef ATUI_NET_HTTP_SIMPLE_H
#define ATUI_NET_HTTP_SIMPLE_H

#include <string>

namespace atui {
namespace net {

// Steps `machine` -- anything with `step( int waitMs )` and `bool busy()`, which is
// `HttpStream` and `ChatRequest` -- until it finishes or `totalSeconds` runs out
// (0 meaning no limit, which is only safe when the machine cannot hang).  Returns
// whether it finished.
//
// The step is a quarter of a tick: long enough that a machine waiting on a socket
// costs nothing measurable, short enough that a machine with bytes ready is not
// held up by the timer.
template <typename Machine>
bool pumpUntilDone( Machine &machine, int totalSeconds )
{
    const int kStepMs = 25;
    int waited = 0;
    while( machine.busy() && ( totalSeconds <= 0 || waited < totalSeconds * 1000 ) )
    {
        machine.step( kStepMs );
        waited += kStepMs;
    }
    return !machine.busy();
}

// A server's error body, flattened to one line and cut short, for the error message
// a person will read: this ends up in a transcript row or a status line, where six
// lines of JSON would push the reason off the screen.
std::string briefForMessage( const std::string &text );

// One blocking HTTP GET, with the same framing, deadlines and error reporting as the
// event-loop transport.  `baseUrl` is a provider's base ("http://host:8080/v1") and
// `path` is joined under it ("models").  On a status that is not 2xx it returns false
// with the server's own words in `error`, and `body` holds whatever came back.
//
// Plain HTTP only, because there is no TLS here: an `https` address is refused rather
// than quietly downgraded, since the request may carry an API key.
bool httpGet( const std::string &baseUrl,
              const std::string &path,
              const std::string &apiKey,
              int seconds,
              std::string &body,
              std::string &error );

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_HTTP_SIMPLE_H
