// The JSON of a chat completion: the request body that goes out, and the merging
// of the streamed fragments that come back.
//
// Both directions of the OpenAI-compatible wire format, in one place, because two
// transports need them: the curl client still in use on the platforms that have it,
// and the event-loop transport replacing it.  The body is a hand-built string, which
// is what the no-exceptions, no-library build this project is written for first can
// carry -- and what makes every field below a decision rather than a default.
//
// C++98, so that the Open Watcom build can use it: no raw string literals, no
// `to_string`, and no assumption that a JSON library exists.
#ifndef ATUI_NET_OAI_BODY_H
#define ATUI_NET_OAI_BODY_H

#include "json_extract.h"      // ToolCallFrag
#include "oai_client.h"        // OaiRequest, OaiResponse, OaiToolCall

#include <string>
#include <vector>

namespace atui {
namespace net {

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
