// Implementation of the shared text wrapping. See wrap.h.
#include "tui/wrap.h"

#include "core/cells.h"

namespace atui {
namespace tui {

void wrapOneOffsets( const std::string &seg, int width,
                     std::vector<std::string> &out, std::vector<size_t> &offsets )
{
    if( width < 1 ) width = 1;
    if( seg.empty() ) { offsets.push_back( 0 ); out.push_back( "" ); return; }
    size_t i = 0, n = seg.size();
    while( i < n )
    {
        // Whole characters only.  `k` is the next character to take, `used` the columns
        // [i, k) already occupies, and `lastSpace` the last place a break was legal.
        // Breaking on bytes instead cuts a multi-byte character in half -- which is not
        // only a ragged row: these same rows are what the transcript exports and what a
        // follow-up request sends back, so half a character becomes corrupt text.
        size_t k = i, used = 0, lastSpace = std::string::npos;
        while( k < n )
        {
            size_t bytes = 1;
            const int cells = atui::core::charCells( seg, k, &bytes );
            // A character that does not fit ends the row -- unless it is the first one,
            // in which case the row takes it anyway: a 2-column ideograph in a 1-column
            // box still has to go somewhere, and emitting nothing would not advance.
            // A mark costing nothing never ends a row, so it stays with the character it
            // modifies instead of opening the next row as a stranded accent.
            if( k > i && used + (size_t) cells > (size_t) width )
                break;
            if( seg[k] == ' ' ) lastSpace = k;
            used += (size_t) cells;
            k += bytes;
        }
        if( k < n && lastSpace != std::string::npos && lastSpace > i )
        {
            offsets.push_back( i );
            out.push_back( seg.substr( i, lastSpace - i ) );
            i = lastSpace + 1;
            while( i < n && seg[i] == ' ' ) ++i;
        }
        else
        {
            offsets.push_back( i );
            out.push_back( seg.substr( i, k - i ) );
            i = k;
        }
    }
}

void wrapOne( const std::string &seg, int width, std::vector<std::string> &out )
{
    std::vector<size_t> ignored;
    wrapOneOffsets( seg, width, out, ignored );
}

std::vector<std::string> wrapText( const std::string &s, int width )
{
    std::vector<std::string> out;
    size_t start = 0;
    while( true )
    {
        size_t nl = s.find( '\n', start );
        std::string seg = ( nl == std::string::npos )
            ? s.substr( start ) : s.substr( start, nl - start );
        wrapOne( seg, width, out );
        if( nl == std::string::npos ) break;
        start = nl + 1;
    }
    return out;
}

} // namespace atui::tui
} // namespace atui
