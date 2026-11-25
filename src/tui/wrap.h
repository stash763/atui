// The app's text wrapping rule, shared by the transcript, the approval box and
// the reasoning renderer.
//
// Rows are measured in screen columns, which is not the number of bytes (see
// `core/cells.h` for the measure and the measurement behind it), and a row never ends in
// the middle of a character.  Lines break on the last space that fits, and a word longer
// than the width is hard-broken at a character boundary.
// C++14, no dependency on the TUI.
#ifndef ATUI_TUI_WRAP_H
#define ATUI_TUI_WRAP_H

#include <string>
#include <vector>

namespace atui {
namespace tui {

// Wrap one logical segment (no '\n' expected) into rows of at most `width`
// columns, appending them to `out`. An empty segment yields one empty row, so
// blank lines survive.
void wrapOne( const std::string &seg, int width, std::vector<std::string> &out );

// The same, reporting where each row came from: `offsets` receives the byte offset in
// `seg` that each appended row starts at.  An editor needs that to turn a caret position,
// which is a byte offset into its buffer, into the row and column to draw it at; without
// it the caret drifts left across every multi-byte character before it.
void wrapOneOffsets( const std::string &seg, int width, std::vector<std::string> &out,
                     std::vector<size_t> &offsets );

// The same for a whole string: each '\n' starts a new row, so blank lines in the
// input stay blank rows in the output.
std::vector<std::string> wrapText( const std::string &s, int width );

} // namespace atui::tui
} // namespace atui

#endif // ATUI_TUI_WRAP_H
