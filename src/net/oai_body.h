// The JSON of a chat completion: the types a request is made of, the body that goes
// out, and the merging of the streamed fragments that come back.
//
// Both directions of the OpenAI-compatible wire format, in one place, so that the one
// transport there is cannot disagree with itself about what the model said.  The body
// is a hand-built string, which is what the no-exceptions, no-library build this
// project was written for first can carry -- and what makes every field below a
// decision rather than a default.
//
// The types are here rather than with a client because they outlive any one of them:
// a conversation, a tool call and a streamed answer are what the turn carries, and
// which socket they travel over is somebody else's business.
//
// C++98, so that the Open Watcom build can use it: no raw string literals, no
// `to_string`, and no assumption that a JSON library exists.
#ifndef ATUI_NET_OAI_BODY_H
#define ATUI_NET_OAI_BODY_H

#include "json_extract.h"      // ToolCallFrag

#include <string>
#include <vector>

namespace atui {
namespace net {

// A function/tool call requested by the assistant -- accumulated from streaming
// delta.tool_calls fragments, or replayed from a conversation history message.
struct OaiToolCall {
    std::string id;
    std::string name;
    std::string arguments;      // raw JSON string produced by the model
};

// A single turn of conversation.
struct OaiMessage {
    std::string role;           // "user", "assistant", "system", "tool"
    std::string content;
    // Optional reasoning/"thinking" text carried with an assistant turn. Only
    // replayed to the model when the "resend thinking" option is enabled.
    std::string reasoning;
    // For role == "assistant" that requested tools:
    std::vector<OaiToolCall> toolCalls;
    // For role == "tool" (a tool execution result):
    std::string toolCallId;
    std::string name;

    OaiMessage() {}
    OaiMessage( const std::string &role_, const std::string &content_ )
        : role( role_ ), content( content_ ) {}
};

// The request parameters.
struct OaiRequest {
    // Defaults in the constructor: `= 0.7` on a member is C++11.
    OaiRequest()
        : temperature( 0.7 ), stream( true ), maxTokens( 0 ),
          timeoutSeconds( 0 ), disableThinking( false )
    {
    }

    std::string model;
    std::vector<OaiMessage> messages;
    double temperature;
    bool stream;
    // Pre-rendered OpenAI "tools" array JSON (see tools::ToolRegistry), or
    // empty to send no tools.
    std::string toolsJson;
    // Optional cap on generated tokens (0 = the server's default). Used by the
    // permission agent, whose answer is a handful of lines: a runaway generation
    // there is a delay in front of every tool call.
    int maxTokens;
    // Wall-clock budget in seconds for this request, overriding the user's
    // request timeout. A permission decision has to come back in about the time
    // it takes to notice; a long prompt must not be cut off while it thinks.
    int timeoutSeconds;
    // Ask the server to answer without a chain of thought. Only the permission
    // agent sets it: a reasoning model spends the entire token budget on
    // `reasoning_content` before it writes a word of the answer, so a judge with
    // a 300-token budget on such a model returns an empty description - which
    // fails closed, but might as well not be asked.
    //
    // All three spellings go out together because servers disagree about which
    // one they read; the ones that do not implement a field ignore it.
    bool disableThinking;
};

// Accumulated result of one streaming request.
struct OaiResponse {
    std::string content;                 // concatenated delta.content
    std::string reasoning;               // concatenated thinking/reasoning text
    std::string finishReason;            // "stop", "tool_calls", ... ("" if none)
    std::vector<OaiToolCall> toolCalls;  // populated when finishReason == tool_calls
};

// The request body for /v1/chat/completions: model, stream flag, sampling, the
// conversation, and the offered tools when there are any.
std::string buildBody( const OaiRequest &req );

// Where the text of a streaming completion goes as it arrives.
class DeltaSink
{
public:
    virtual ~DeltaSink() {}
    // A piece of the answer (`thinking` when it is the model's reasoning rather
    // than its reply).  Called per payload, in order, with non-empty text only.
    virtual void onDelta( const std::string &text, bool thinking ) = 0;
};

// Applies one `data:` payload of a streaming chat completion to `out` -- which may
// be null when the caller wants the text but not the assembled answer -- reporting
// each content and reasoning piece to `sink` as it is found, and folding in any
// tool-call fragments and finish reason this payload carries.
//
// This is the whole OpenAI-shaped half of streaming, and it is here rather than in
// a transport so that the two transports (curl, and the event-loop state machine
// replacing it) cannot drift apart on the question that matters: what the model
// actually said.
void applyStreamDelta( const std::string &payload, OaiResponse *out,
                       DeltaSink *sink );

// Folds one batch of streaming `delta.tool_calls` fragments into a response, keyed
// by the fragment index: `id` and the function name arrive once, `arguments` arrive
// in pieces that have to be concatenated in the order they came.
void mergeToolFrags( OaiResponse &resp, const std::vector<ToolCallFrag> &frags );

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_OAI_BODY_H
