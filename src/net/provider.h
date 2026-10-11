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

// --- how long a prompt may take ---------------------------------------------
// A model on slow hardware can sit on a prompt for minutes on end before the
// first token arrives, so the wait is a setting rather than a constant. It
// bounds one request - a tool round is a request of its own and gets the whole
// wait again - and the app only has one in flight at a time.
//
// Lives here rather than with the HTTP client because the client needs libcurl:
// this module is built either way, so the option is readable, editable and
// persistable in both builds.
const int kMinRequestMinutes = 1;
const int kMaxRequestMinutes = 1440;   // a day

// What a fresh install waits: two hours. Enough for a big prompt on a
// CPU-served model to reason its way through, short enough that a dead
// connection does not sit there until tomorrow. The dialog marks this.
const int kDefaultRequestMinutes = 120;

// Minutes to wait for a request before giving up on it. Backed by a
// process-wide value; seeded by settings::loadAll() from
// [app] request_minutes, written by saveAll(), and adjusted by
// Options > Request timeout. Read per request, so a change applies to the next
// prompt. Reference it as a variable.
int &requestMinutes();

// Waits outside the range above are not worth honoring: under a minute a slow
// model never gets to answer, and past a day a hung connection just holds the
// transcript open. Every value - config file, dialog, edit by hand - comes
// through here.
int clampRequestMinutes( int minutes );

} // namespace net
} // namespace atui

#endif // ATUI_NET_PROVIDER_H
