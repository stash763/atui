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

#include <stddef.h>   // size_t for SettleScan, without leaning on <string> for it
#include <string>
#include <vector>

#include "tui/md_style.h"

namespace atui {
namespace tui {

// Render `md` into rows wrapped to `width` columns. `width` < 4 is clamped up.
std::vector<Line> render( const std::string &md, int width );

// Incremental settling for an append-only document.
//
// render() costs the length of the document, so an answer that grows a chunk
// per redraw and settles nothing costs its length times its duration -- the
// box's "high cpu utilization occurs as the size of the response grows". The
// text is append-only, though, and a prefix ending just before a non-blank
// column-0 line that follows a blank line is final *for this renderer*: every
// construct it knows ends at a blank line (a paragraph flushes, a quote needs
// its marker on every line, a table needs adjacent rows, list items carry
// literal markers) and no code fence is open there. Whatever is appended next
// can only start below that line, so the prefix can be rendered once and kept.
//
// The one document-global construct is a link definition ("[label]: url"),
// which text above it may reference; a document that shows one is marked dead
// and settles nothing, which is the old full-render behavior.
//
// feed() the growing text as often as you like -- bytes before `scan` are
// never looked at again -- and render [0, boundary) once plus [boundary, end)
// per redraw: the per-redraw cost becomes the current paragraph, not the
// answer.
struct SettleScan {
    size_t   scan;      // how far through the text the walk has read
    size_t   boundary;  // start of the newest final prefix (0: none yet)
    bool     dead;      // document holds a link definition: settle nothing
    bool     prevBlank; // the line before `scan` was blank
    char     fence;     // fence char of an open code fence, 0 outside one
    int      fenceLen;  // its marker length, which a closer must match

    void init();
    size_t feed( const std::string &md );   // returns the boundary (0 if dead)
};

} // namespace atui::tui
} // namespace atui

#endif // ATUI_TUI_MARKDOWN_H
