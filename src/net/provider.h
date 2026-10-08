// Provider settings for OpenAI-compatible chat servers (llama.cpp, Ollama,
// OpenRouter, ...). Several named providers can be configured; exactly one is
// active and is what requests use. The store is a process-wide singleton,
// seeded from the environment, editable at runtime (Options ▸ Providers and the
// "Switch model" menu) and persisted by settings::saveAll(). Requests snapshot
// the active provider.
// C++14, network module (ATUI_HAVE_NETWORK).
#ifndef ATUI_NET_PROVIDER_H
#define ATUI_NET_PROVIDER_H

#include <string>
#include <vector>

namespace atui {
namespace net {

struct ProviderSettings {
    std::string name;      // unique id shown in the UI, e.g. "ollama"
    std::string baseUrl;   // e.g. "http://127.0.0.1:11434/v1"
    std::string apiKey;    // optional bearer token
    std::string model;     // selected model (one of `models` when possible)
    double temperature;
    // When true, an assistant turn's thinking text is stored in the
    // conversation and replayed to the model on later turns. Off by default
    // (thinking is display-only), matching OpenAI behavior.
    bool thinkingResend;
    // Model ids offered by this provider (from GET /models or the config file);
    // the Providers dialog lists them, `model` is the one actually used.
    std::vector<std::string> models;
};

// Fresh settings built from ATUI_* environment variables (or the built-in
// defaults). Not the store: every call returns a new value.
ProviderSettings defaultProviderSettings();

// All configured providers, in display order. Never empty. Indices are only
// valid until the next add/remove, so re-read them rather than cache them, and
// prefer providerSettings() over a stored reference.
std::vector<ProviderSettings>& providers();

// Index of the active provider within providers() (always valid).
std::size_t& activeProviderIndex();

// The active provider: the endpoint and model a chat request uses.
ProviderSettings& providerSettings();

// Switching the active provider. setActiveProvider() clamps the index.
// setActiveProviderByName() matches case-insensitively and returns false
// (changing nothing) when no provider has that name.
void setActiveProvider( std::size_t index );
bool setActiveProviderByName( const std::string &name );

// Store editing. addProvider() appends an entry based on
// defaultProviderSettings() under a unique name and returns its index.
// removeProvider() keeps the active index valid and refuses to delete the last
// remaining provider. findProvider() returns 0 when the name is unknown.
std::size_t addProvider( const std::string &name );
bool removeProvider( std::size_t index );
ProviderSettings *findProvider( const std::string &name );

// Model handling. setModels() replaces the model list and keeps the selected
// model: it is re-added at the front when the endpoint does not report it (a
// refresh must not silently change which model is used), and only defaults to
// the first id when nothing was selected yet.
void setModels( ProviderSettings &provider,
                const std::vector<std::string> &models );
// Selects a model, appending it to the list when unknown (hand-typed ids).
void selectModel( ProviderSettings &provider, const std::string &model );

// Model lists persist as one comma-separated config value.
std::vector<std::string> parseModelList( const std::string &text );
std::string formatModelList( const std::vector<std::string> &models );

// A provider name not used yet: "base", then "base-2", "base-3", ...
std::string uniqueProviderName( const std::string &base );

// Replaces the whole store (used when reading a config file). An empty list is
// ignored; `activeName` picks the active provider, case-insensitively, and the
// first entry is used when it does not match.
void replaceProviders( const std::vector<ProviderSettings> &list,
                       const std::string &activeName );

} // namespace net
} // namespace atui

#endif // ATUI_NET_PROVIDER_H
