// Provider settings for the remote llama.cpp server (OpenAI-compatible
// endpoint). A single process-wide instance, seeded from the environment and
// editable at runtime via the Options ▸ Providers dialog. Snapshot per request.
// C++14, network module (ATUI_HAVE_NETWORK).
#ifndef ATUI_NET_PROVIDER_H
#define ATUI_NET_PROVIDER_H

#include <string>

namespace atui {
namespace net {

struct ProviderSettings {
    std::string baseUrl;      // e.g. "http://127.0.0.1:8080/v1"
    std::string apiKey;       // optional bearer token
    std::string model;        // served model name
    double temperature;
    // When true, an assistant turn's thinking text is stored in the
    // conversation and replayed to the model on later turns. Off by default
    // (thinking is display-only), matching OpenAI behavior.
    bool thinkingResend;
};

// Process-wide settings. Initialized on first use from ATUI_* environment
// variables (ATUI_BASE_URL, ATUI_API_KEY, ATUI_MODEL, ATUI_TEMPERATURE),
// then mutable so dialog edits take effect on the next request.
ProviderSettings& providerSettings();

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_PROVIDER_H