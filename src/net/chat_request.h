// One streaming chat completion, as a state machine.
//
// This is the request atui makes every time the user presses Enter, written so that
// it can be made from inside the event loop: `begin()` starts it, `step(waitMs)` is
// called whenever the TUI has a moment, and the sink is told what arrives -- text as
// it streams, then exactly one finish.  Nothing here blocks, so the screen keeps
// drawing and a keypress still reaches the prompt while a 27-billion-parameter model
// decides what it thinks.
//
// It is also the whole of one round-trip, with no memory of previous rounds: the
// conversation, the tool loop and the asking-about-permissions live in ChatTurn,
// which drives one of these at a time.
//
// What it adds over the transport underneath:
//
//   * building the body and the URL, so the caller thinks in requests, not strings;
//   * SSE framing and the OpenAI-shaped extraction from it (shared with the curl
//     client through oai_body.cpp, so the two transports cannot disagree about what
//     the model said);
//   * a status that is not 2xx becoming an error *with the server's words in it*.
//     The curl path did not look at the status at all, so a 401 arrived as a
//     perfectly successful empty answer -- the single most confusing failure this
//     program produced, because it was indistinguishable from a model that had
//     nothing to say.
//
// C++98, for the compilers atui is built with.  Nothing here assumes who is calling:
// `step()` is driven by whoever owns the request, and no state is shared between two
// of them.
#ifndef ATUI_NET_CHAT_REQUEST_H
#define ATUI_NET_CHAT_REQUEST_H

#include "http.h"
#include "oai_body.h"      // DeltaSink
#include "oai_client.h"    // OaiRequest, OaiResponse
#include "sse.h"

#include <string>

namespace atui {
namespace net {

class ChatRequest : public HttpStream::Sink
                  , public SseStream::Sink
                  , public DeltaSink
{
public:
    class Sink
    {
    public:
        virtual ~Sink() {}
        // Part of the answer, in order.  `thinking` marks the model's reasoning,
        // which is displayed and collapsed rather than treated as the reply.
        virtual void onText( const std::string &text, bool thinking ) = 0;
        // Exactly once per request, after the last piece of text.  `ok` says the
        // answer arrived complete; when it is false, `error` is for a person.
        virtual void onFinished( bool ok, const std::string &error ) = 0;
    };

    ChatRequest()
        : sink_( 0 )
        , reported_( false )
        , ok_( false )
        , status_( 0 )
    {
    }

    // Sends the request and starts reading the answer.  `baseUrl` is a provider's
    // base URL in the form the settings store it (`http://host:8080/v1`), and
    // `apiKey` may be empty, in which case no Authorization header goes out.
    // Returns false only if nothing was started, with `error` filled.
    bool begin( const std::string &baseUrl,
                const std::string &apiKey,
                const OaiRequest &req,
                Sink *sink,
                int connectSeconds,
                int idleSeconds,
                std::string *error );

    // Advances the request; see HttpStream::step.
    void step( int waitMs ) { http_.step( waitMs ); }

    bool busy() const { return http_.busy(); }

    // Ends it early -- escape pressed -- and reports that to the sink once.
    void abort( const std::string &reason ) { http_.abort( reason ); }

    // The assembled answer: content, reasoning, tool calls and finish reason.  Only
    // complete once the sink has been told it is finished.
    const OaiResponse &response() const { return out_; }

    // What the request decided, for a caller with no sink.
    //
    // A sink is how a stepped request reports as it goes, and every caller of
    // `net::pumpUntilDone` -- the permission agent, the model list, `httpGet` -- is a
    // caller that asks afterwards instead.  Until now asking was not possible: the end
    // of a request was handed to a sink that did not exist and the reason went with it,
    // so `--judge-check` reported "the agent could not describe the command" with an
    // empty reply and no way to find out whether the server refused, timed out, or
    // closed the connection.  Measured, that is exactly what it looked like.
    //
    // `hasResult()` is false while the request is still running and true once it has
    // ended by any route -- including `abort()`, which reports too.
    bool hasResult() const { return reported_; }
    bool ok() const { return reported_ && ok_; }
    const std::string &error() const { return error_; }

private:
    // HttpStream::Sink
    virtual void onStatus( int code );
    virtual void onBody( const char *data, std::size_t len );
    virtual void onFinish( bool ok, const std::string &reason );
    // SseStream::Sink
    virtual void onPayload( const std::string &payload );
    virtual void onDone();
    // DeltaSink
    virtual void onDelta( const std::string &text, bool thinking );

    // Reports the end exactly once, whatever path got here, and remembers it for the
    // accessors above.
    void finish( bool ok, const std::string &error );
    // A non-2xx answer's text, in the error message: short, and with the middle of
    // a long JSON body cut rather than wrapped.
    static std::string briefBody( const std::string &body );

    HttpStream http_;
    SseStream  sse_;
    OaiResponse out_;
    std::string errorBody_;     // what came back when the status said no
    Sink *sink_;
    bool reported_;             // the end has been reported, once
    bool ok_;                   // ... and what it said: succeeded, or not
    std::string error_;         // and the reason, for a caller with no sink
    int status_;
};

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_CHAT_REQUEST_H
