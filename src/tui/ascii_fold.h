// What a seven-bit terminal is shown, and nothing else.
//
// atui's requirement is that transcript-visible text is ASCII. The reason is not
// purism: the ports are talking to terminals whose character set is an 8-bit codepage
// (CP437, CP850), and UTF-8 emitted into one arrives as two characters -- the em dash
// that a model writes as U+2014 shows up as the Greek-looking "ΓÇö" on the OS/2 box,
// which is not a rendering choice but bytes being read as the wrong thing.
//
// So text is folded on the way to the screen: recognised characters become their ASCII
// spelling ("-" for a dash, "'" for a curly quote, "..." for an ellipsis), characters
// that have no ASCII spelling become "?", and the bytes of an encoding this function was
// not meant to see -- a CP850 byte that is not a valid UTF-8 lead -- are passed through
// untouched, because keystrokes from an 8-bit keyboard arrive as those bytes and turning
// them into "?" would damage what the user typed.
//
// The fold belongs to the view rather than to the append methods for one streaming
// reason: an answer arrives in chunks, and a multi-byte sequence can be split between
// two of them. The blocks hold the text as it arrived and the fold happens when the view
// lays them out, at which point the chunks have been joined again.

#ifndef ATUI_TUI_ASCII_FOLD_H
#define ATUI_TUI_ASCII_FOLD_H

#include <string>

namespace atui {
namespace tui {

// Whether this build's terminal needs folding. OS/2 and DOS say yes: the console is an
// 8-bit codepage. Others say no for now, which is a decision rather than a measurement
// -- a Windows box sitting on CP850 would want this too, and the Windows port has not
// been run on one yet (TESTING.md says so).
bool foldTerminalText();

// Fold unconditionally, whatever the platform. Split out so it can be tested on a
// machine whose terminal does not need it.
std::string foldToAscii( const std::string &in );

// foldToAscii when foldTerminalText() says it is needed, and a copy otherwise. This is
// what the transcript calls.
std::string foldForTerminal( const std::string &in );

} // namespace tui
} // namespace atui

#endif // ATUI_TUI_ASCII_FOLD_H
