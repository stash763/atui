// A strict JSON validator, for the test suite only.
//
// atui builds its request JSON out of string literals by hand. One tool's schema was
// missing a closing brace -- around `properties`, which put `required` inside it and left
// the tool object one level open, so everything after that tool in the `tools` array sat
// where a key was expected. `ca4503e` lost it while splitting a description into
// per-platform strings, so from that commit on, every request that offered a tool was
// invalid on every platform. Every test that looked at the request looked at *pieces* of
// it; nothing ever read the whole thing back, so the first thing to reject it was a real
// server, which said 500 and sent the diagnosis off to examine the wrong machine
// (TESTING.md has the story).
//
// So: build the document, then ask whether a strict parser would accept it.
//
// This is deliberately not the product's parser. The product decodes JSON *strings*
// leniently -- an unknown escape keeps both characters, because a model answering with a
// Windows path should not lose it -- and that is the right behaviour for input from a
// model. A validator that inherited that leniency could not catch a document a server
// would refuse, which is the whole job here. Strict means strict: no trailing commas, no
// control characters in strings, no NaN, nothing after the top-level value.
//
// C++98, like the harness it serves.
#ifndef ATUI_TESTS_JSON_VALID_H
#define ATUI_TESTS_JSON_VALID_H

#include <cstddef>
#include <string>

namespace atui_test {

namespace jsondetail {

inline void skipWs( const std::string &s, std::size_t &i )
{
    while( i < s.size() &&
           ( s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' ) )
        ++i;
}

inline bool isHex( char c )
{
    return ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' );
}

inline bool parseString( const std::string &s, std::size_t &i )
{
    if( i >= s.size() || s[i] != '"' )
        return false;
    ++i;
    while( i < s.size() )
    {
        const char c = s[i];
        if( c == '"' )
        {
            ++i;
            return true;
        }
        if( c == '\\' )
        {
            ++i;
            if( i >= s.size() )
                return false;
            if( s[i] == 'u' )
            {
                for( int k = 0; k < 4; ++k )
                {
                    ++i;
                    if( i >= s.size() || !isHex( s[i] ) )
                        return false;
                }
                ++i;
                continue;
            }
            if( s[i] != '"' && s[i] != '\\' && s[i] != '/' && s[i] != 'b' &&
                s[i] != 'f' && s[i] != 'n' && s[i] != 'r' && s[i] != 't' )
                return false;
            ++i;
            continue;
        }
        if( (unsigned char)c < 0x20 )
            return false;      // a control character has to be escaped in a string
        ++i;                   // any other byte, including UTF-8, is allowed as it stands
    }
    return false;              // ran out without a closing quote
}

inline bool parseNumber( const std::string &s, std::size_t &i )
{
    if( i < s.size() && s[i] == '-' )
        ++i;
    std::size_t digits = 0;
    while( i < s.size() && s[i] >= '0' && s[i] <= '9' )
    {
        ++i;
        ++digits;
    }
    if( digits == 0 )
        return false;
    if( digits > 1 && s[i - digits] == '0' )
        return false;                                  // 012 is not a number
    if( i < s.size() && s[i] == '.' )
    {
        ++i;
        digits = 0;
        while( i < s.size() && s[i] >= '0' && s[i] <= '9' )
        {
            ++i;
            ++digits;
        }
        if( digits == 0 )
            return false;                              // "1." is not a number
    }
    if( i < s.size() && ( s[i] == 'e' || s[i] == 'E' ) )
    {
        ++i;
        if( i < s.size() && ( s[i] == '+' || s[i] == '-' ) )
            ++i;
        digits = 0;
        while( i < s.size() && s[i] >= '0' && s[i] <= '9' )
        {
            ++i;
            ++digits;
        }
        if( digits == 0 )
            return false;
    }
    return true;
}

inline bool parseValue( const std::string &s, std::size_t &i );

inline bool parseObject( const std::string &s, std::size_t &i )
{
    ++i;                                             // '{'
    skipWs( s, i );
    if( i < s.size() && s[i] == '}' )
    {
        ++i;
        return true;
    }
    for( ;; )
    {
        skipWs( s, i );
        if( !parseString( s, i ) )
            return false;
        skipWs( s, i );
        if( i >= s.size() || s[i] != ':' )
            return false;
        ++i;
        skipWs( s, i );
        if( !parseValue( s, i ) )
            return false;
        skipWs( s, i );
        if( i < s.size() && s[i] == ',' )
        {
            ++i;
            continue;
        }
        break;
    }
    if( i >= s.size() || s[i] != '}' )
        return false;                                 // includes "trailing comma"
    ++i;
    return true;
}

inline bool parseArray( const std::string &s, std::size_t &i )
{
    ++i;                                             // '['
    skipWs( s, i );
    if( i < s.size() && s[i] == ']' )
    {
        ++i;
        return true;
    }
    for( ;; )
    {
        skipWs( s, i );
        if( !parseValue( s, i ) )
            return false;
        skipWs( s, i );
        if( i < s.size() && s[i] == ',' )
        {
            ++i;
            continue;
        }
        break;
    }
    if( i >= s.size() || s[i] != ']' )
        return false;
    ++i;
    return true;
}

inline bool parseValue( const std::string &s, std::size_t &i )
{
    if( i >= s.size() )
        return false;
    const char c = s[i];
    if( c == '{' )
        return parseObject( s, i );
    if( c == '[' )
        return parseArray( s, i );
    if( c == '"' )
        return parseString( s, i );
    if( c == 't' )
    {
        if( s.compare( i, 4, "true" ) != 0 )
            return false;
        i += 4;
        return true;
    }
    if( c == 'f' )
    {
        if( s.compare( i, 5, "false" ) != 0 )
            return false;
        i += 5;
        return true;
    }
    if( c == 'n' )
    {
        if( s.compare( i, 4, "null" ) != 0 )
            return false;
        i += 4;
        return true;
    }
    if( c == '-' || ( c >= '0' && c <= '9' ) )
        return parseNumber( s, i );
    return false;
}

} // namespace jsondetail

// Where a strict parser gives up, or npos if it would take the whole document. "The
// request is not JSON" and "the request is not JSON, at 2194, here is the text" are not
// the same thing to read at the end of a run.
inline std::size_t jsonFailurePos( const std::string &s )
{
    std::size_t i = 0;
    jsondetail::skipWs( s, i );
    if( !jsondetail::parseValue( s, i ) )
        return i;
    jsondetail::skipWs( s, i );
    return i == s.size() ? std::string::npos : i;      // nothing after the value
}

// A printable window around a failure position, control characters turned into `?` so
// the note stays on one line. Lossy on purpose: the position is the precise part, this
// is so a reader can see what the parser was looking at.
inline std::string jsonFailureContext( const std::string &s, std::size_t pos,
                                       std::size_t window = 60 )
{
    const std::size_t from = pos > window ? pos - window : 0;
    std::string out = s.substr( from, pos - from + window );
    for( std::size_t i = 0; i < out.size(); ++i )
        if( (unsigned char)out[i] < 0x20 || (unsigned char)out[i] == 0x7f )
            out[i] = '?';
    return out;
}

// Would a strict JSON parser take this whole string as one document?
inline bool jsonIsValid( const std::string &s )
{
    return jsonFailurePos( s ) == std::string::npos;
}

} // namespace atui_test

#endif // ATUI_TESTS_JSON_VALID_H
