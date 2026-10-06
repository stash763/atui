// Application settings persistence.
//
// Ties the IniFile helper to the existing settings singletons. This module is
// the only place that knows the file schema (section/key names), so the parts
// that *use* settings (net/provider, tui/theme) need no changes: they keep
// exposing their normal accessors and this glue reads/writes them.
//
//   [ui]
//   theme = dark
//
//   [provider]
//   base_url = http://127.0.0.1:8080/v1
//   api_key  =
//   model    = llama
//   temperature = 0.7
//
// Precedence: env vars seed the singletons first (see provider.cpp,
// theme.cpp); a config file, when present, overrides those.
#ifndef ATUI_SETTINGS_SETTINGS_H
#define ATUI_SETTINGS_SETTINGS_H

#include <string>

namespace atui {
namespace settings {

// Path of the config file. Honors $ATUI_CONFIG, then $XDG_CONFIG_HOME/atui/,
// then $HOME/.atui.ini, then a relative "atui.ini".
std::string configPath();

// Read the config file (if present) and apply it to providerSettings() and
// the theme. Returns true if a file was read. Safe to call when absent.
bool loadAll();

// Write providerSettings() and the current theme to the config file, keeping
// any unrelated keys that were already in it. Returns true on success.
bool saveAll();

} // namespace atui::settings
} // namespace atui

#endif // ATUI_SETTINGS_SETTINGS_H
