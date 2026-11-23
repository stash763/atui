// Definitions for the JSON string decoder.  See jsonstr.h.
#include "jsonstr.h"

namespace atui {
namespace core {

namespace {

const unsigned kSurrogateHighBegin = 0xD800;
const unsigned kSurrogateHighEnd   = 0xDBFF;
const unsigned kSurrogateLowBegin  = 0xDC00;
const unsigned kSurrogateLowEnd    = 0xDFFF;

// The replacement character U+FFFD, in the encoding this function produces.
// A string literal rather than three char literals: 0xEF is not a value a signed
// `char` can hold, and Watcom says so when it is written as one.
const char kReplacement[] = "\xEF\xBF\xBD";

inline bool hexDigit( char c, unsigned &d )
{
    if( c >= '0' && c <= '9' ) { d = unsigned( c - '0' );        return true; }
    if( c >= 'a' && c <= 'f' ) { d = unsigned( c - 'a' ) + 10u;  return true; }
    if( c >= 'A' && c <= 'F' ) { d = unsigned( c - 'A' ) + 10u;  return true; }
    return false;
}

// Reads exactly four hex digits at s[i] into `cp`.  False when they are not
// there, having read nothing -- a `\u` that is not a code point stays on screen
// as the four characters it was, rather than half-converting.
bool hex4( const std::string &s, std::size_t i, std::size_t end, unsigned &cp )
{
    if( i + 4 > end )
        return false;
    unsigned v = 0;
    for( int k = 0; k < 4; ++k ) {
        unsigned d;
        if( !hexDigit( s[i + k], d ) )
            return false;
        v = ( v << 4 ) | d;
    }
    cp = v;
    return true;
}

// Appends the UTF-8 encoding of a code point.  The four-byte arm is only
// reachable from a joined surrogate pair, which is the point: writing three
// bytes for half a surrogate -- what this did before it joined them -- puts
// invalid UTF-8 into the terminal, and an emoji arrived as two broken ones.
void appendUtf8( std::string &out, unsigned cp )
{
    if( cp < 0x80 ) {
        out += char( cp );
    } else if( cp < 0x800 ) {
        out += char( 0xC0 | ( cp >> 6 ) );
        out += char( 0x80 | ( cp & 0x3F ) );
    } else if( cp < 0x10000 ) {
        out += char( 0xE0 | ( cp >> 12 ) );
        out += char( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
        out += char( 0x80 | ( cp & 0x3F ) );
    } else {
        out += char( 0xF0 | ( cp >> 18 ) );
        out += char( 0x80 | ( ( cp >> 12 ) & 0x3F ) );
        out += char( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
        out += char( 0x80 | ( cp & 0x3F ) );
    }
}

} // namespace

bool decodeJsonString( const std::string &s, std::size_t &i,
                       std::size_t end, std::string &out )
{
    if( end > s.size() )
        end = s.size();
    if( i >= end || s[i] != '"' )
        return false;
    ++i;

    bool ok = true;
    while( i < end ) {
        const char c = s[i];
        if( c == '"' ) {
            ++i;                       // past the closing quote
            return ok;
        }
        if( c != '\\' ) {
            out += c;
            ++i;
            continue;
        }

        // A backslash.  Every way of being wrong below keeps the characters and
        // carries on, because dropping one is how a path became a different path.
        if( i + 1 >= end ) {
            out += '\\';               // trailing backslash: the literal is broken
            i = end;
            break;
        }
        const char e = s[i + 1];
        switch( e ) {
            case '"':  out += '"';  i += 2; break;
            case '\\': out += '\\'; i += 2; break;
            case '/':  out += '/';  i += 2; break;
            case 'b':  out += '\b'; i += 2; break;
            case 'f':  out += '\f'; i += 2; break;
            case 'n':  out += '\n'; i += 2; break;
            case 'r':  out += '\r'; i += 2; break;
            case 't':  out += '\t'; i += 2; break;

            case 'u': {
                unsigned cp;
                if( !hex4( s, i + 2, end, cp ) ) {
                    // Not a code point.  Emit `\u` and whatever hex-looking text
                    // followed it, so the damage is visible instead of silent.
                    out += "\\u";
                    i += 2;
                    for( int k = 0; k < 4 && i < end; ++k ) {
                        unsigned d;
                        if( !hexDigit( s[i], d ) )
                            break;
                        out += s[i];
                        ++i;
                    }
                    ok = false;
                    break;
                }
                i += 6;
                if( cp >= kSurrogateHighBegin && cp <= kSurrogateHighEnd ) {
                    unsigned low;
                    if( i + 6 <= end && s[i] == '\\' && s[i + 1] == 'u' &&
                        hex4( s, i + 2, end, low ) &&
                        low >= kSurrogateLowBegin && low <= kSurrogateLowEnd ) {
                        i += 6;
                        appendUtf8( out, 0x10000u +
                                        ( ( cp - kSurrogateHighBegin ) << 10 ) +
                                        ( low - kSurrogateLowBegin ) );
                    } else {
                        // A high half with no low half denotes nothing.  U+FFFD is
                        // the honest answer: something was here and cannot be
                        // shown.  Dropping it loses a character without saying so.
                        out += kReplacement;
                        ok = false;
                    }
                } else if( cp >= kSurrogateLowBegin && cp <= kSurrogateLowEnd ) {
                    out += kReplacement;
                    ok = false;
                } else {
                    appendUtf8( out, cp );
                }
                break;
            }

            default:
                // The rule of this file, in two characters.
                out += '\\';
                out += e;
                i += 2;
                break;
        }
    }
    return false;                      // ran out of input: never closed
}

} // namespace atui::core
} // namespace atui
