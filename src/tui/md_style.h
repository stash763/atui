// Shared styling vocabulary for the transcript renderer.
//
// A rendered line is a sequence of Spans; each Span carries a small integer
// Role that the TranscriptView translates to a concrete (theme-aware) color in
// mapColor(). Roles are kept as plain uchar so they can be handed straight to
// TView::writeStr(), which routes them through mapColor(uchar).
#ifndef ATUI_TUI_MD_STYLE_H
#define ATUI_TUI_MD_STYLE_H

#include <string>
#include <vector>

namespace atui {
namespace tui {

enum Role
{
    rNormal = 0,   // default prose / answer text

    // Transcript block kinds (kept from the pre-markdown renderer).
    rUser,         // the "> you" prompt line
    rThinking,     // reasoning block body
    rHead,         // collapsible block header
    rStatus,       // status line
    rTool,         // a tool call recorded in the transcript ("$ command")
    rToolOut,      // what that tool returned

    // Inline markdown.
    rHeading,      // # heading text
    rBold,
    rItalic,
    rBoldItalic,
    rCodeInline,   // `inline code`
    rLink,         // link label (URL is usually not shown)
    rQuote,        // blockquote text
    rMarker,       // list bullet / number, footnote marker, rule
    rStrike,       // ~~strikethrough~~ (shown dim)
    rDim,          // de-emphasised (titles, footnote text)

    // Code blocks.
    rCode,         // un-highlighted code text
    rKeyword,
    rType,
    rString,
    rChar,
    rComment,
    rNumber,
    rFunction,
    rPreproc,
    rOperator,

    // The input area at the bottom of the transcript. It is drawn by the same
    // view, so it shares this numbering rather than keeping one of its own.
    rInput,        // the text being typed
    rInputSep      // the rule above it
};

// One past the last role: the per-role attribute tables in theme.cpp are sized
// with it, so a new role has to be added to every one of them, and here.
const int rRoleCount = rInputSep + 1;

// A run of text that shares a single Role.
struct Span
{
    std::string text;
    unsigned char role;

    Span() : text(), role( rNormal ) {}
    Span( const std::string &t, unsigned char r ) : text( t ), role( r ) {}
};

// One fully laid-out display row: the spans that fill it (already wrapped).
struct Line
{
    std::vector<Span> spans;

    // Background role for the whole row. Normally rNormal, but code rows use
    // rCode so the code background fills the full width edge-to-edge (and
    // blank lines inside a code block still read as code) instead of only
    // under the drawn text.
    unsigned char bg;

    Line() : spans(), bg( rNormal ) {}

    // Width in display columns (byte-based, matching the wrap logic).
    int width() const
    {
        int w = 0;
        for( size_t i = 0; i < spans.size(); ++i )
            w += (int) spans[i].text.size();
        return w;
    }
};

} // namespace atui::tui
} // namespace atui

#endif // ATUI_TUI_MD_STYLE_H
