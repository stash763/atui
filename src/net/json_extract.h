// Minimal JSON + SSE helpers for the OpenAI-compatible /v1/chat/completions
// streaming response. C++14-gated (network-only).
#ifndef ATUI_NET_JSON_EXTRACT_H
#define ATUI_NET_JSON_EXTRACT_H

#include <string>
#include <vector>

namespace atui {
namespace net {

// Extracts the delta.content value from an OpenAI streaming JSON body:
//   {"id":...,"object":"chat.completion.chunk","role":"assistant",
//    "choices":[{"index":0,"delta":{"role":"assistant","content":"..."},
//               "finish_reason":"..."}]}
// Returns "" when the delta.content field is absent or null (the final
// finish_reason chunk).
std::string deltaContent( const std::string &json );

// Returns the choices[0].finish_reason value ("stop", "tool_calls", ...), or
// "" when absent or null.
std::string finishReason( const std::string &json );

// One "delta.tool_calls" array element from a streaming chunk. Fragments for
// the same call arrive across chunks (id+name first, then argument pieces)
// and are merged by index by the caller.
struct ToolCallFrag {
    int index;
    std::string id;
    std::string name;
    std::string arguments;
    ToolCallFrag() : index( 0 ) {}
};

// Parses delta.tool_calls in a streaming chunk, appending fragments to `out`.
// Returns true if at least one fragment was found.
bool parseToolCallFrags( const std::string &json,
                         std::vector<ToolCallFrag> &out );

// Escapes a string so it is valid inside a JSON "string".
std::string jsonEscape( const std::string &s );

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_JSON_EXTRACT_H
