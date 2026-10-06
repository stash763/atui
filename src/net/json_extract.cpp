// Definitions for the minimal JSON + SSE helpers.
#include "json_extract.h"

#include <cstring>

namespace atui {
namespace net {

namespace {

inline bool isWs( char c )
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Advance past whitespace within [i, end).
void skipWs( const std::string &s, size_t &i, size_t end )
{
    while( i < end && isWs( s[i] ) ) ++i;
}

// Decode a JSON string literal whose opening '"' is at s[i]; advances i past
// the closing quote and returns the decoded value. Bounded by end.
std::string decodeString( const std::string &s, size_t &i, size_t end )
{
    std::string out;
    if( i >= end || s[i] != '"' ) return out;
    ++i;
    while( i < end && s[i] != '"' )
    {
        if( s[i] == '\\' && i + 1 < end )
        {
            char e = s[i + 1];
            switch( e )
            {
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                default:   out += e;    break;
            }
            i += 2;
        }
        else
        {
            out += s[i];
            ++i;
        }
    }
    if( i < end && s[i] == '"' ) ++i; // consume closing quote
    return out;
}

// Given s[i] == '{', return the index of the matching '}' (string-aware),
// or npos.
size_t matchBrace( const std::string &s, size_t i, size_t end )
{
    if( i >= end || s[i] != '{' ) return std::string::npos;
    int depth = 0;
    while( i < end )
    {
        char c = s[i];
        if( c == '"' )
        {
            ++i;
            while( i < end && s[i] != '"' )
            {
                if( s[i] == '\\' ) ++i;
                ++i;
            }
            if( i >= end ) return std::string::npos;
            ++i; // past closing quote
            continue;
        }
        if( c == '{' ) ++depth;
        else if( c == '}' ) { --depth; if( depth == 0 ) return i; }
        ++i;
    }
    return std::string::npos;
}

// Scan for a member named `key` (no quotes) whose string-token boundary is
// respected; if found, return the index just after the ':' (value start),
// else npos.
size_t valuePosForKey( const std::string &s, size_t i, size_t end,
                       const char *key )
{
    size_t klen = std::strlen( key );
    while( i < end )
    {
        if( s[i] == '"' )
        {
            size_t start = i + 1;
            size_t j = start;
            while( j < end && s[j] != '"' )
            {
                if( s[j] == '\\' ) ++j;
                ++j;
            }
            if( j < end && s[j] == '"' )
            {
                if( (size_t)( j - start ) == klen &&
                    std::strncmp( &s[start], key, klen ) == 0 )
                {
                    size_t k = j + 1;
                    skipWs( s, k, end );
                    if( k < end && s[k] == ':' ) return k + 1;
                }
                i = j + 1;
            }
            else
            {
                i = j;
            }
        }
        else
        {
            ++i;
        }
    }
    return std::string::npos;
}

bool readStr( const std::string &s, size_t rangeStart, size_t rangeEnd,
              const char *key, std::string &out )
{
    size_t vp = valuePosForKey( s, rangeStart, rangeEnd, key );
    if( vp == std::string::npos ) return false;
    skipWs( s, vp, rangeEnd );
    if( vp >= rangeEnd || s[vp] != '"' ) return false;
    out = decodeString( s, vp, rangeEnd );
    return true;
}

bool readInt( const std::string &s, size_t rangeStart, size_t rangeEnd,
              const char *key, int &out )
{
    size_t vp = valuePosForKey( s, rangeStart, rangeEnd, key );
    if( vp == std::string::npos ) return false;
    skipWs( s, vp, rangeEnd );
    if( vp >= rangeEnd ) return false;
    const char *b = s.c_str() + vp;
    const char *limit = s.c_str() + rangeEnd;
    char *e = 0;
    long v = std::strtol( b, &e, 10 );
    if( e == b || e > limit ) return false;
    out = (int) v;
    return true;
}

} // anonymous namespace

std::string jsonEscape( const std::string &s )
{
    std::string out;
    out.reserve( s.size() + 8 );
    for( char c : s )
    {
        switch( c )
        {
            case '"':  out += "\\\"";   break;
            case '\\': out += "\\\\";   break;
            case '\n': out += "\\n";    break;
            case '\r': out += "\\r";    break;
            case '\t': out += "\\t";    break;
            case '\b': out += "\\b";    break;
            case '\f': out += "\\f";    break;
            default:   out += c;        break;
        }
    }
    return out;
}

std::string deltaContent( const std::string &json )
{
    // Only look for "delta" once; the OpenAI chunk format has a single
    // delta object per chunk, so the first occurrence is the right one.
    size_t dpos = json.find( "\"delta\"" );
    if( dpos == std::string::npos )
        return std::string();

    // Within that object, find "content" followed by a value.
    size_t cpos = json.find( "\"content\"", dpos );
    if( cpos == std::string::npos )
        return std::string();

    // Find the colon after the key.
    size_t colon = json.find( ':', cpos + 9 ); // strlen("\"content\"") == 9
    if( colon == std::string::npos )
        return std::string();

    // Skip whitespace.
    size_t i = colon + 1;
    while( i < json.size() &&
           ( json[i] == ' ' || json[i] == '\t' || json[i] == '\n' ||
             json[i] == '\r' ) )
        ++i;

    // If it is not a quoted string, the value is null (or absent).
    if( i >= json.size() || json[i] != '"' )
        return std::string();

    // Decode the JSON string value.
    ++i;
    std::string out;
    out.reserve( 64 );
    while( i < json.size() && json[i] != '"' )
    {
        if( json[i] == '\\' && i + 1 < json.size() )
        {
            char e = json[i + 1];
            switch( e )
            {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '"': out += '"';  break;
                case '\\': out += '\\'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                default: out += e;     break;
            }
            i += 2;
        }
        else
        {
            out += json[i];
            ++i;
        }
    }
    return out;
}

std::string finishReason( const std::string &json )
{
    size_t pos = json.find( "\"finish_reason\"" );
    if( pos == std::string::npos )
        return std::string();
    size_t i = pos + 15; // strlen("\"finish_reason\"")
    skipWs( json, i, json.size() );
    if( i >= json.size() || json[i] != ':' ) return std::string();
    ++i;
    skipWs( json, i, json.size() );
    if( i >= json.size() || json[i] != '"' ) return std::string(); // null/absent
    return decodeString( json, i, json.size() );
}

bool parseToolCallFrags( const std::string &json,
                         std::vector<ToolCallFrag> &out )
{
    size_t dp = json.find( "\"delta\"" );
    if( dp == std::string::npos ) dp = 0;
    size_t tc = json.find( "\"tool_calls\"", dp );
    if( tc == std::string::npos ) return false;

    size_t i = tc + 12; // strlen("\"tool_calls\"")
    size_t end = json.size();
    skipWs( json, i, end );
    if( i >= end || json[i] != ':' ) return false;
    ++i;                        // past ':'
    skipWs( json, i, end );
    if( i >= end || json[i] != '[' ) return false;
    ++i; // past '['

    bool found = false;
    while( i < end )
    {
        skipWs( json, i, end );
        if( i >= end || json[i] == ']' ) break;
        if( json[i] != '{' ) break;
        size_t os = i;
        size_t oe = matchBrace( json, os, end );
        if( oe == std::string::npos ) break;

        ToolCallFrag frag;
        readInt( json, os, oe, "index", frag.index );
        readStr( json, os, oe, "id", frag.id );

        size_t fp = valuePosForKey( json, os, oe, "function" );
        if( fp != std::string::npos )
        {
            skipWs( json, fp, oe );
            if( fp < oe && json[fp] == '{' )
            {
                size_t fes = fp;
                size_t fees = matchBrace( json, fes, oe );
                if( fees != std::string::npos )
                {
                    readStr( json, fes, fees, "name", frag.name );
                    readStr( json, fes, fees, "arguments", frag.arguments );
                }
            }
        }

        out.push_back( frag );
        found = true;

        i = oe + 1;
        skipWs( json, i, end );
        if( i < end && json[i] == ',' ) ++i;
    }
    return found;
}

} // namespace atui::net
} // namespace atui
