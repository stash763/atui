// Text shaping for showing a tool call in the UI.
//
// Model-produced argument strings can contain anything: newlines, tabs, control
// bytes, or megabytes of pasted text. Before a call is rendered in the
// transcript it goes through one of these, so a preview is safe to print and
// bounded in size. Header-only, C++14.
#ifndef ATUI_TOOLS_PREVIEW_H
#define ATUI_TOOLS_PREVIEW_H

#include <cstddef>
#include <string>

namespace atui {
namespace tools {

// Cap for a call preview: long enough for any real command, short enough that a
// model pasting a whole file into an argument cannot flood the transcript.
const std::size_t kMaxPreview = 1000;

// `s` collapsed onto one line: line breaks and tabs become spaces, other control
// bytes become '?'. At most `max` bytes are kept, with "..." when anything was
// dropped. Used where a single line is available (dialog summaries).
inline std::string oneLineClip( const std::string &s, std::size_t max )
{
    std::string out;
    for( std::size_t i = 0; i < s.size() && out.size() < max; ++i )
    {
        char c = s[i];
        if( c == '\n' || c == '\r' || c == '\t' ) out += ' ';
        else if( (unsigned char) c < 32 )         out += '?';
        else                                      out += c;
    }
    if( s.size() > max ) out += "...";
    return out;
}

// `s` kept as printed text, line for line: CR is dropped, tabs become spaces and
// other control bytes become '?'. Capped at `max` bytes. Used for command
// previews, where the line breaks of a multi-line command are the point.
inline std::string previewClip( const std::string &s, std::size_t max )
{
    std::string out;
    for( std::size_t i = 0; i < s.size() && out.size() < max; ++i )
    {
        char c = s[i];
        if( c == '\r' )                                continue;
        if( c == '\t' )                                out += ' ';
        else if( c == '\n' || (unsigned char) c >= 32 ) out += c;
        else                                            out += '?';
    }
    if( s.size() > max ) out += "...";
    while( !out.empty() && ( out[out.size() - 1] == ' '
           || out[out.size() - 1] == '\n' ) )
        out.erase( out.size() - 1 );
    return out;
}

} // namespace atui::tools
} // namespace atui

#endif // ATUI_TOOLS_PREVIEW_H
