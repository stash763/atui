// The model list: `GET /models`, and the ids inside it.
//
// The Providers dialog asks for this while it is being edited, which is a caller with
// no event loop to step -- so it goes through the blocking face of the transport
// (`httpGet`) rather than the state machine the chat path uses.  Same framing, same
// deadlines, same rule about `https`.
//
// Network-only: it asks a server, and a build without a transport has nothing to ask.
#ifndef ATUI_NET_MODELS_H
#define ATUI_NET_MODELS_H

#include <string>
#include <vector>

namespace atui {
namespace net {

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

} // namespace net
} // namespace atui

#endif // ATUI_NET_MODELS_H
