// Implementation of the thinking-text segmentation. See thinking.h.
#include "tui/thinking.h"
#include "tui/markdown.h"
#include "tui/wrap.h"

#include <cstddef>

namespace atui {
namespace tui {

namespace {

// The start of a fence line: three or more identical '`' or '~' after optional
// indentation. Returns the marker character (0 when the line is not a fence),
// the length of the run, and whatever follows the marker (the info string, or
// blanks for a closing fence).
char fenceMarker( const std::string &line, std::size_t &len, std::string &rest )
{
    std::size_t i = 0;
    while( i < line.size() && ( line[i] == ' ' || line[i] == '\t' ) ) ++i;
    if( i >= line.size() ) return 0;

    const char c = line[i];
    if( c != '`' && c != '~' ) return 0;

    std::size_t n = 0;
    while( i + n < line.size() && line[i + n] == c ) ++n;
    len  = n;
    rest = line.substr( i + n );
    return n >= 3 ? c : 0;
}

bool isBlank( const std::string &s )
{
    for( std::size_t i = 0; i < s.size(); ++i )
        if( s[i] != ' ' && s[i] != '\t' ) return false;
    return true;
}

// The first word of a fence info string: "python title=foo" -> "python".
std::string infoWord( std::string rest )
{
    std::size_t a = rest.find_first_not_of( " \t" );
    if( a == std::string::npos ) return std::string();
    std::size_t b = rest.find_first_of( " \t", a );
    return ( b == std::string::npos ) ? rest.substr( a ) : rest.substr( a, b - a );
}

struct Row
{
    std::string text;    // the line, newline included when the source had one
    bool        code;    // true: part of a code body; the fence lines
    std::string info;    // themselves are dropped, so what is left is shown

    Row() : text(), code( false ), info() {}
};

} // anonymous namespace

std::vector<ThinkingSeg> splitThinking( const std::string &text )
{
    std::vector<ThinkingSeg> out;
    if( text.empty() ) return out;

    // Pass 1: mark every line as reasoning or as part of a code body. A fence
    // with no closing fence runs to the end - that is a stream caught mid-block.
    std::vector<Row> rows;
    char        open    = 0;
    std::size_t openLen = 0;
    std::string info;

    std::size_t pos = 0;
    while( pos < text.size() )
    {
        const std::size_t nl   = text.find( '\n', pos );
        const bool        last = ( nl == std::string::npos );
        std::string line = last ? text.substr( pos ) : text.substr( pos, nl - pos );
        pos += line.size() + ( last ? 0 : 1 );

        std::size_t len  = 0;
        std::string rest;
        const char  m = fenceMarker( line, len, rest );

        if( open && m == open && len >= openLen && isBlank( rest ) )
        {
            open    = 0;      // closing fence: the line itself is not code
            openLen = 0;
            continue;
        }
        if( m && !open )
        {
            open    = m;      // opening fence: also not part of the body
            openLen = len;
            info    = infoWord( rest );
            continue;
        }

        Row r;
        r.text = line;
        if( !last ) r.text += '\n';
        r.code = ( open != 0 );
        r.info = info;
        rows.push_back( r );
    }

    // Pass 2: consecutive lines of the same kind become one segment.
    std::size_t i = 0;
    while( i < rows.size() )
    {
        ThinkingSeg seg;
        seg.code = rows[i].code;
        seg.info = rows[i].info;
        while( i < rows.size() && rows[i].code == seg.code )
        {
            seg.text += rows[i].text;
            ++i;
        }
        out.push_back( seg );
    }
    return out;
}

std::string fencedMarkdown( const ThinkingSeg &seg )
{
    // A fence longer than any run inside cannot be closed by the code itself
    // (CommonMark closes a fence only with a run at least as long).
    std::size_t longest = 0, run = 0;
    for( std::size_t i = 0; i < seg.text.size(); ++i )
    {
        if( seg.text[i] == '`' ) { ++run; if( run > longest ) longest = run; }
        else                     run = 0;
    }
    const std::size_t n = ( longest + 1 < 3 ) ? 3 : longest + 1;
    const std::string fence( n, '`' );

    std::string body = seg.text;
    if( !body.empty() && body[body.size() - 1] != '\n' ) body += '\n';

    return fence + seg.info + "\n" + body + fence + "\n";
}

std::vector<Line> renderThinkingBody( const std::string &text, int width )
{
    std::vector<Line> out;
    const std::vector<ThinkingSeg> segs = splitThinking( text );

    for( size_t s = 0; s < segs.size(); ++s )
    {
        if( !segs[s].code )
        {
            // One trailing newline belongs to the segment boundary (the fence
            // line dropped beside it), not to the layout: keeping it would open
            // a blank row in the middle of the reasoning.
            std::string body = segs[s].text;
            if( s + 1 < segs.size() && !body.empty()
                && body[body.size() - 1] == '\n' )
                body.erase( body.size() - 1 );

            std::vector<std::string> rows = wrapText( body, width - 3 );
            for( size_t i = 0; i < rows.size(); ++i )
            {
                Line l;
                l.spans.push_back( Span( "   " + rows[i], rThinking ) );
                out.push_back( l );
            }
            continue;
        }

        std::vector<Line> code = render( fencedMarkdown( segs[s] ), width - 3 );
        for( size_t i = 0; i < code.size(); ++i )
        {
            Line l;
            l.spans.push_back( Span( "   ", rCode ) );     // the three-column indent
            for( size_t k = 0; k < code[i].spans.size(); ++k )
                l.spans.push_back( code[i].spans[k] );
            l.bg = rCode;   // code background edge-to-edge, as in an answer
            out.push_back( l );
        }
    }
    return out;
}

} // namespace atui::tui
} // namespace atui
