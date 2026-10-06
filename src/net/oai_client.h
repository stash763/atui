// OpenAI-compatible streaming HTTP client (libcurl + SSE).
// C++14, network-only (ATUI_HAVE_NETWORK).
#ifndef ATUI_NET_OAI_CLIENT_H
#define ATUI_NET_OAI_CLIENT_H

#include <string>
#include <vector>
#include <utility>

namespace atui {
namespace net {

// A function/tool call requested by the assistant (accumulated from streaming
// delta.tool_calls fragments) or replayed in a conversation history message.
struct OaiToolCall {
    std::string id;
    std::string name;
    std::string arguments; // raw JSON string produced by the model
};

// A single turn of conversation.
struct OaiMessage {
    std::string role;    // "user", "assistant", "system", "tool"
    std::string content;
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
    std::string model;
    std::vector<OaiMessage> messages;
    double temperature = 0.7;
    bool stream = true;
    // Pre-rendered OpenAI "tools" array JSON (see tools::ToolRegistry), or
    // empty to send no tools.
    std::string toolsJson;
};

// Accumulated result of one streaming request.
struct OaiResponse {
    std::string content;                 // concatenated delta.content
    std::string finishReason;            // "stop", "tool_calls", ... ("" if none)
    std::vector<OaiToolCall> toolCalls;  // populated when finishReason == tool_calls
};

// Invoked (on the worker thread) with each extracted content token.
typedef void (*TokenCallback)( const char *token, void *ctx );

// Performs a streaming chat-completion request. Blocks until the stream is
// complete or the connection fails. `cb`/`ctx` receive each delta.content
// token. When `out` is non-null it receives the assembled content, the final
// finish_reason and any tool calls.
// Returns true on success (HTTP 2xx and stream finished), false on error.
class OaiClient {
public:
    bool request( const std::string &baseUrl,
                  const std::string &apiKey,
                  const OaiRequest &req,
                  TokenCallback cb,
                  void *ctx,
                  OaiResponse *out = 0 );
};

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_OAI_CLIENT_H
