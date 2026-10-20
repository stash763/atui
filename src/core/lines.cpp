// See lines.h for why this exists.

#include "core/lines.h"

namespace atui {
namespace core {

bool readLine( std::istream &in, std::string &out )
{
    out.clear();

    // failbit, not eofbit, decides: get() sets both when there is nothing left,
    // and asking for eof separately would say "there is still a line" about the
    // last one in a file that does not end with a newline -- which is true, but
    // only until the character has actually been read.
    char c = '\0';
    bool sawAnything = false;
    for ( ;; ) {
        in.get( c );
        if( in.fail() )
            break;
        sawAnything = true;
        if( '\n' == c )
            break;
        out += c;
    }

    if( !out.empty() && '\r' == out[out.size() - 1] )
        out.erase( out.size() - 1, 1 );

    return sawAnything;
}

} // namespace atui::core
} // namespace atui
