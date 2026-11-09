// The prompt file and its editor: where the user's instructions live.
//
// Six dialog fields were never going to hold what a person wants to
// tell a model, and the honest editor for many lines of text is a
// text editor. So the instructions are a file -- this module knows
// the file -- and the dialog that manages them offers that file to
// the machine's editor: $EDITOR on Unix (vi when the variable keeps
// silent), e.exe on OS/2, notepad.exe on the Windows that is coming.
// The user gets a real editor by being on a machine that has one.
//
// The file is the store. The dialog reads it through loadPromptsFile
// into core::agentPrompts().extra, the editor writes it, and reload
// is the bridge between the two while the editor is still open --
// which on OS/2 and Windows it always is, because those editors
// detach the moment they start. Nothing here writes the file except
// the editor and ensurePromptsFile's first-time empty touch.
//
// The spawning is the same one run_command's fallback uses -- the
// C library's system(), shell and all -- because a detached editor
// is exactly what cmd.exe and /bin/sh are good at: on Unix system()
// waits for the editor and the reload happens the moment it closes;
// on OS/2 and Windows the command returns as the editor's window
// appears, and Reload is the user's own hand on the same bridge.
//
// C++98, standard library only.
#ifndef ATUI_CORE_PROMPTFILE_H
#define ATUI_CORE_PROMPTFILE_H

#include <string>

namespace atui {
namespace core {

// The editor command from the config file ([prompts] editor), empty
// when unset. Seeded by settings::loadAll; the dialog does not
// change it -- the person who wants a different editor edits the
// config file, which is now the interface for this kind of thing.
std::string &promptsEditorOverride();

// The editor that will be used: the override, else $EDITOR, else the
// platform's own answer -- "e.exe" on OS/2, "notepad.exe" on
// Windows, "vi" everywhere else.
std::string promptsEditor();

// Creates the file empty when it does not exist (the first Edit in
// an editor should meet a file, not an error). True when the file is
// there afterwards.
bool ensurePromptsFile( const std::string &path, std::string &error );

// Reads the file over agentPrompts().extra -- lines as they stand,
// CR trimmed, a trailing blank line dropped. A file that cannot be
// read leaves the current lines alone and says why. There is no line
// cap in memory: the cap, such as it is, belongs to the context
// window, and that is nobody's business until the request 400s.
bool loadPromptsFile( const std::string &path, std::string &error );

// Opens the file in promptsEditor(). Creates the file first, builds
// the command line with the platform's quoting, and runs it through
// system(). Where the editor is synchronous (Unix), reloads the file
// before returning so the caller sees the edits; elsewhere, the file
// is whatever the editor left, and the caller reloads when told to.
bool openPromptsEditor( const std::string &path, std::string &error );

// --- pure parts, testable without a shell or an editor -------------

// Wraps one argument for /bin/sh: single quotes throughout, with the
// classic '\'' escape for a path that contains a single quote.
std::string quotePosixArg( const std::string &arg );

// Wraps one argument for cmd.exe: double quotes when the path needs
// them (spaces, or an existing quoteless special), never touching an
// argument already wrapped.
std::string quoteCmdArg( const std::string &arg );

// Truncates a line for a preview field of `width` columns, marking
// the cut with the one character that always means "more": a
// non-empty line never fits in zero columns, and empty ones fit
// anywhere.
std::string fitPreviewLine( const std::string &line, std::size_t width );

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_PROMPTFILE_H
