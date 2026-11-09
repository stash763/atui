// Application settings persistence.
//
// Ties the IniFile helper to the existing settings singletons. This module is
// the only place that knows the file schema (section/key names), so the parts
// that *use* settings (net/provider, tui/theme) need no changes: they keep
// exposing their normal accessors and this glue reads/writes them.
//
//   [permissions]
//   mode          = agent      # ask_all | exact | agent
//   network       = agent      # agent | ask | allow
//   judge_model   = mini-q4    # empty = the active provider's model
//   judge_seconds = 5          # how long one judgement may take
//   log           = true       # append each decision to permissions.log
//
//   [undo]
//   enabled   = true       # keep a copy of a file before changing it
//   dir       =            # empty = <install directory>/undo
//   max_bytes = 1048576    # whole-journal cap; oldest entries are dropped first
//   max_file  = 1048576    # above this there is no copy, and edits are asked
//
//   [tools]
//   command_timeout = 300  # seconds a run_command may take; 0 = no limit
//   pass_env =             # names to give a command anyway, comma separated
//
//   [ui]
//   theme = dark
//   thinking_expand = false
//   wheel_lines = 3
//
//   [prompts]
//   host_brief = true        # include the auto host/OS brief (only
//                            # false is written; default is true)
//   file =                   # instruction-lines file; default is
//                            # "atui_prompts.txt" beside this one
//   editor =                 # editor for that file; default is
//                            # $EDITOR, e.exe on OS/2, notepad.exe
//                            # on Windows
//
//   [app]
//   provider = ollama            # active provider
//   request_minutes = 120        # how long one request may take
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

// What a fresh install scrolls at: one line a notch reads as stalled next to
// how fast a wheel turns. The options dialog marks this as the default.
const int kDefaultWheelLines = 3;

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

// Supported range for promptLines(): the height cap, in wrapped lines, of the
// PM front's prompt strip. The strip grows with the text being typed and stops
// here; below the cap nothing ever scrolls, above it the text box scrolls to
// follow the caret. Two lines is the smallest strip that can show a wrap
// happening, twenty is taller than the prompt on any machine this ships to
// should ever be.
const int kMinPromptLines = 2;
const int kMaxPromptLines = 20;

// What the prompt caps at on a fresh install: ten lines keeps a normal
// multi-paragraph message fully visible while leaving most of a 768-pixel
// screen to the transcript.
const int kDefaultPromptLines = 10;

// How tall the prompt strip may grow, in wrapped lines. Backed by a
// process-wide value; seeded by loadAll() from [ui] prompt_lines, written by
// saveAll(), and set by Options > Settings. The PM driver reads it on every
// resize of the strip, so a change applies as soon as the text reflows.
// Reference it as a variable.
int &promptLines();

// Same clamping discipline as the wheel: a value from a hand-edited config
// file or a mistyped dialog entry should still leave a usable window.
int clampPromptLines( int lines );

// The file holding the agent's instruction lines ([prompts] file, or
// "atui_prompts.txt" beside the config file when unset). The file is
// the store: the Options > Prompts dialog reads it and offers it to
// the machine's editor, and nothing else writes it. Reference by
// value: the override can be reloaded.
std::string promptsFilePath();

// The raw [prompts] file override, empty when unset. Exposed for
// the save path and the dialog's hint line.
std::string &promptsFileOverride();

} // namespace atui::settings
} // namespace atui

#endif // ATUI_SETTINGS_SETTINGS_H
