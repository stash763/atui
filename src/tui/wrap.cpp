// Implementation of the shared text wrapping. See wrap.h.
#include "tui/wrap.h"

namespace atui {
namespace tui {

void wrapOne( const std::string &seg, int width,
              std::vector<std::string> &out )
{
    if( width < 1 ) width = 1;
    if( seg.empty() ) { out.push_back( "" ); return; }
    size_t i = 0, n = seg.size();
    while( i < n )
    {
        size_t k = i, cnt = 0, lastSpace = std::string::npos;
        while( k < n && cnt < (size_t) width )
        {
            if( seg[k] == ' ' ) lastSpace = k;
            ++k; ++cnt;
        }
        if( k < n && lastSpace != std::string::npos && lastSpace > i )
        {
            out.push_back( seg.substr( i, lastSpace - i ) );
            i = lastSpace + 1;
            while( i < n && seg[i] == ' ' ) ++i;
        }
        else
        {
            out.push_back( seg.substr( i, k - i ) );
            i = k;
        }
    }
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
