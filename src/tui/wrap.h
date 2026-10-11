// The app's text wrapping rule, shared by the transcript, the approval box and
// the reasoning renderer.
//
// One byte is one display column (the app never measured cells), lines break on
// the last space that fits, and a word longer than the width is hard-broken.
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

// The same for a whole string: each '\n' starts a new row, so blank lines in the
// input stay blank rows in the output.
std::vector<std::string> wrapText( const std::string &s, int width );

} // namespace atui::tui
} // namespace atui

#endif // ATUI_TUI_WRAP_H
