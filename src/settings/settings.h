// Application settings persistence.
//
// Ties the IniFile helper to the existing settings singletons. This module is
// the only place that knows the file schema (section/key names), so the parts
// that *use* settings (net/provider, tui/theme) need no changes: they keep
// exposing their normal accessors and this glue reads/writes them.
//
//   [ui]
//   theme = dark
//   thinking_expand = false
//   wheel_lines = 3
//
//   [app]
//   provider = ollama            # active provider
//
//   [provider:ollama]            # one section per provider
//   name     = ollama            # display name (case preserved here)
//   base_url = http://10.0.0.5:11434/v1
//   api_key  =
//   model    = qwen3:30b         # selected model for this provider
//   models   = llama3.2,qwen3:30b  # cached ids from GET /models
//   temperature = 0.7
//   thinking_resend = false
//
// A legacy single [provider] section is still read and becomes the provider
// named "default".
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

// Whether reasoning/"thinking" blocks are expanded by default in the chat
// transcript. Backed by a process-wide flag; seeded by loadAll() and written
// by saveAll(). Toggle at runtime with Ctrl-T. Reference this as a variable.
bool &thinkingExpandAll();

// Supported range for wheelLines(): one line per notch at slowest, and at
// fastest a notch that still lands inside a normal transcript instead of
// flinging the view past everything at once.
const int kMinWheelLines = 1;
const int kMaxWheelLines = 30;

// How far one mouse wheel notch scrolls the chat transcript, in lines.
// Backed by a process-wide value; seeded by loadAll() from [ui] wheel_lines,
// written by saveAll(), and adjusted by Options > Scrolling. The transcript
// reads it per wheel event, so a change applies immediately. Reference it as a
// variable.
int &wheelLines();

// Wheel steps outside the range above do nobody any good (a notch that moves
// nothing, or one that overshoots the whole transcript), so every value - from
// a config file, from a dialog, from an edit by hand - comes through here.
int clampWheelLines( int lines );

} // namespace atui::settings
} // namespace atui

#endif // ATUI_SETTINGS_SETTINGS_H
