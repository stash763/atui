// A pragmatic CommonMark-ish markdown renderer for the chat transcript.
//
// render() turns one block of markdown into fully laid-out display rows:
// each row is a vector of styled Spans, word-wrapped to the given width. It
// covers the markdown AI assistants actually emit — headings, strong/emphasis,
// inline code, fenced code blocks (syntax highlighted), bullet / ordered /
// task lists (with nesting), blockquotes, thematic breaks, GFM tables, inline
// and reference links, autolinks, images and footnotes — with a span-aware
// wrapper that keeps styling across wraps.
#ifndef ATUI_TUI_MARKDOWN_H
#define ATUI_TUI_MARKDOWN_H

#include <string>
#include <vector>

#include "tui/md_style.h"

namespace atui {
namespace tui {

// Render `md` into rows wrapped to `width` columns. `width` < 4 is clamped up.
std::vector<Line> render( const std::string &md, int width );

} // namespace atui::tui
} // namespace atui

#endif // ATUI_TUI_MARKDOWN_H
