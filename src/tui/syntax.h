// Lightweight, per-line syntax highlighter for fenced code blocks.
//
// It is deliberately simple (no full grammar): identifier/keyword tables,
// string/char scanning, line and block comments, numbers and (for C/C++) a
// preprocessor line. Multi-line constructs (block comments, triple-quoted
// strings, JS template literals) are carried across lines via SynState so a
// whole code fence highlights correctly.
#ifndef ATUI_TUI_SYNTAX_H
#define ATUI_TUI_SYNTAX_H

#include <string>
#include <vector>

#include "tui/md_style.h"

namespace atui {
namespace tui {

enum Lang
{
    LangPlain = 0,   // no highlighting (just rCode)
    LangC,           // C / C++ / CUDA / header
    LangPython,
    LangJS,          // JavaScript / TypeScript / JSON-ish? (JSON is separate)
    LangJSON,
    LangBash,        // sh / bash / zsh / shell
    LangGo,
    LangRust
};

// Map a code-fence info string ("python", "py", "c++", "sh", ...) to a Lang.
// Unknown values map to LangPlain.
Lang detectLang( const std::string &info );

// Cross-line state for the highlighter.
struct SynState
{
    int mode;             // 0 = normal, 1 = block comment, 2 = multiline string
    std::string term;     // terminator for the open construct ("*/", "\"\"\"", "'''", "`")
    bool escapedNewline;  // bash: a trailing backslash continues a string

    SynState() : mode( 0 ), escapedNewline( false ) {}
    void reset() { mode = 0; term.clear(); escapedNewline = false; }
};

// Tokenize one source line, appending styled spans to `out`. `st` carries state
// across lines. `lang` selects the profile. An empty line still yields one
// (possibly empty) span so callers can lay it out.
void highlightLine( Lang lang, const std::string &code,
                    SynState &st, std::vector<Span> &out );

} // namespace atui::tui
} // namespace atui

#endif // ATUI_TUI_SYNTAX_H
