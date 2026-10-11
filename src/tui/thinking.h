// Splitting reasoning/"thinking" text into prose and code.
//
// Reasoning is streamed as plain text, but models routinely think *in* code
// blocks - a snippet they are about to write, a diff, a command. Showing those
// as prose loses the one part that only reads well as code, so the transcript
// segments the text with splitThinking() and renders code segments through the
// markdown/syntax pipeline (highlighting and the code background), leaving
// everything else as plain reasoning.
//
// C++14, no dependency on tvision.
#ifndef ATUI_TUI_THINKING_H
#define ATUI_TUI_THINKING_H

#include <string>
#include <vector>

#include "tui/md_style.h"

namespace atui {
namespace tui {

struct ThinkingSeg
{
    bool        code;   // true: fenced code block, false: plain reasoning
    std::string text;   // prose as-is / code with the fence lines removed
    std::string info;   // code only: the fence info string, e.g. "cpp"

    ThinkingSeg() : code( false ), text(), info() {}
};

// Break `text` into prose and fenced-code segments. A fence is three or more
// backticks or tildes; its info string is whatever follows the marker. The
// segments reproduce the text in order - only the fence lines themselves are
// dropped from a code segment. An unterminated fence, which is normal while a
// block is still streaming, runs to the end of the text.
std::vector<ThinkingSeg> splitThinking( const std::string &text );

// A code segment as markdown: wrapped in a fence its own content cannot close,
// so handing it to the markdown renderer highlights it as code and keeps the
// language (the first word of the info string) for the syntax highlighter.
std::string fencedMarkdown( const ThinkingSeg &seg );

// Laid-out rows for a reasoning body: prose as plain `rThinking` text, fenced
// code blocks through the markdown renderer (highlighted, on the code
// background) because models think in code and a snippet of it as prose is
// unreadable. Every row is indented three columns and no fence markers are
// shown; rows come back with their spans and background role set, so the caller
// only has to say which block they belong to.
std::vector<Line> renderThinkingBody( const std::string &text, int width );

} // namespace atui::tui
} // namespace atui

#endif // ATUI_TUI_THINKING_H
