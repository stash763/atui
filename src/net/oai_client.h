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
    std::string reasoning;               // concatenated thinking/reasoning text
    std::string finishReason;            // "stop", "tool_calls", ... ("" if none)
    std::vector<OaiToolCall> toolCalls;  // populated when finishReason == tool_calls
};

// Invoked (on the worker thread) with each streamed token. `thinking` is true
// for reasoning/"thinking" deltas and false for normal answer content.
typedef void (*TokenCallback)( const char *token, bool thinking, void *ctx );

// Performs a streaming chat-completion request. Blocks until the stream is
// complete, the connection fails, or the request has been waiting longer than
// requestMinutes() (Options > Request timeout). `cb`/`ctx` receive each
// delta.content token. When `out` is non-null it receives the assembled
// content, the final finish_reason and any tool calls.
// Returns true on success (HTTP 2xx and stream finished), false on error; when
// `err` is non-null it then holds a short human-readable reason, naming the
// timeout limit when that is what gave up.
class OaiClient {
public:
    bool request( const std::string &baseUrl,
                  const std::string &apiKey,
                  const OaiRequest &req,
                  TokenCallback cb,
                  void *ctx,
                  OaiResponse *out = 0,
                  std::string *err = 0 );
};

// Fetches the provider's model list via a synchronous GET of
// <baseUrl>/models (OpenAI-compatible; works on Ollama and llama.cpp).
// On success fills `models` with the ids in server order and returns true.
// On failure returns false and sets `err` to a short human-readable reason.
// Blocks the calling thread (short timeouts); call it from the UI thread for
// the settings dialog.
bool fetchModels( const std::string &baseUrl,
                  const std::string &apiKey,
                  std::vector<std::string> &models,
                  std::string &err );

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_OAI_CLIENT_H
