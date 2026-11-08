// Pure parts of the hands seam. See platform/appman.h.
//
// The parsers here are the whole reason the tools can trust their
// arguments: a handle the model invented must fail here rather than
// become WinPostMsg's problem, and every rule this file states is
// tested in the selftest -- no desktop, no PM, no excuses.
#include "platform/appman.h"

namespace atui {
namespace platform {

namespace {

// The shared grammar: optional surrounding spaces, an optional 0x
// prefix (hex digits only with it, decimal without), at least one
// digit, nothing else. `allowZero` tells whether "0" is a value:
// a window handle of zero is no window, a message number of zero is
// just a strange message.
bool parseNumber( const std::string &text, unsigned long &out,
                  bool allowZero )
{
    std::string s;
    for ( std::string::size_type i = 0; i != text.size(); ++i )
        if ( text[ i ] != ' ' && text[ i ] != '\t' )
            s += text[ i ];

    if ( s.empty() )
        return false;

    bool hex = false;
    std::string::size_type i = 0;
    if ( s.size() > 2 && s[ 0 ] == '0'
         && ( s[ 1 ] == 'x' || s[ 1 ] == 'X' ) )
    {
        hex = true;
        i = 2;
    }

    unsigned long v = 0;
    const unsigned long base = hex ? 16UL : 10UL;
    for ( ; i != s.size(); ++i )
    {
        const char c = s[ i ];
        unsigned long d = 0;
        if ( c >= '0' && c <= '9' )
            d = (unsigned long)( c - '0' );
        else if ( hex && c >= 'a' && c <= 'f' )
            d = (unsigned long)( c - 'a' ) + 10UL;
        else if ( hex && c >= 'A' && c <= 'F' )
            d = (unsigned long)( c - 'A' ) + 10UL;
        else
            return false;
        // Growth past 32 bits is refused, not wrapped: a truncated
        // handle would silently name somebody else's window.
        if ( v > ( 0xFFFFFFFFUL - d ) / base )
            return false;
        v = v * base + d;
    }

    if ( !allowZero && v == 0 )
        return false;
    out = v;
    return true;
}

} // anonymous namespace

bool parseWindowHandle( const std::string &text, unsigned long &out )
{
    return parseNumber( text, out, false );
}

bool parseMessageWord( const std::string &text, unsigned long &out )
{
    return parseNumber( text, out, true );
}

} // namespace atui::platform
} // namespace atui
