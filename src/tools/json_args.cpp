// Minimal flat-JSON object reader. See json_args.h.
#include "tools/json_args.h"

#include "core/jsonstr.h"     // decodeJsonString: the one string decoder

#include <cctype>

namespace atui {
namespace tools {

namespace {

enum Kind { kNone, kString, kLiteral };

void skipWs( const std::string &s, std::size_t &i )
{
    while( i < s.size() && std::isspace( (unsigned char) s[i] ) )
        ++i;
}

// String literals are decoded by core/, in one place: see core/jsonstr.h for why
// an unknown escape keeps both of its characters, and why a Windows path depends
// on that.  All this adds is the bound -- these scans work on whole strings.
bool readStringLiteral( const std::string &s, std::size_t &i, std::string &out )
{
    return core::decodeJsonString( s, i, s.size(), out );
}

// Skip a nested object/array at s[i], honouring strings so braces inside them
// do not unbalance the scan. Advances i past the closing bracket.
bool skipNested( const std::string &s, std::size_t &i )
{
    char open = s[i], close = ( open == '{' ) ? '}' : ']';
    int depth = 0;
    while( i < s.size() )
    {
        char c = s[i];
        if( c == '"' ) { std::string tmp; if( !readStringLiteral( s, i, tmp ) ) return false; continue; }
        if( c == open )  { ++depth; ++i; continue; }
        if( c == close ) { --depth; ++i; if( depth == 0 ) return true; continue; }
        ++i;
    }
    return false;
}

// Find a top-level member named `key`. On a match sets `kind` + `val` (decoded
// for strings, raw token for literals) and returns true. Nested values are
// skipped (never returned).
bool findMember( const std::string &obj, const std::string &key,
                 Kind &kind, std::string &val )
{
    kind = kNone;
    std::size_t i = 0;
    skipWs( obj, i );
    if( i >= obj.size() || obj[i] != '{' ) return false;
    ++i;
    skipWs( obj, i );
    if( i < obj.size() && obj[i] == '}' ) return false;   // empty object

    while( i < obj.size() )
    {
        skipWs( obj, i );
        std::string k;
        if( !readStringLiteral( obj, i, k ) ) return false;
        skipWs( obj, i );
        if( i >= obj.size() || obj[i] != ':' ) return false;
        ++i;
        skipWs( obj, i );
        if( i >= obj.size() ) return false;

        char c = obj[i];
        if( c == '"' )
        {
            std::string v;
            if( !readStringLiteral( obj, i, v ) ) return false;
            if( k == key ) { kind = kString; val = v; return true; }
        }
        else if( c == '{' || c == '[' )
        {
            if( !skipNested( obj, i ) ) return false;
        }
        else
        {
            std::size_t start = i;
            while( i < obj.size() && obj[i] != ',' && obj[i] != '}' &&
                   obj[i] != ']' && !std::isspace( (unsigned char) obj[i] ) )
                ++i;
            if( k == key ) { kind = kLiteral; val = obj.substr( start, i - start ); return true; }
        }

        skipWs( obj, i );
        if( i >= obj.size() ) return false;
        if( obj[i] == ',' ) { ++i; continue; }
        if( obj[i] == '}' ) return false;   // reached the end, key absent
        return false;                        // malformed
    }
    return false;
}

bool truthy( const std::string &v, bool &out )
{
    if( v == "true"  || v == "1" ) { out = true;  return true; }
    if( v == "false" || v == "0" ) { out = false; return true; }
    return false;
}

} // anonymous namespace

bool jsonStringArg( const std::string &obj, const std::string &key,
                    std::string &out )
{
    Kind kind; std::string val;
    if( findMember( obj, key, kind, val ) && kind == kString )
    {
        out = val;
        return true;
    }
    return false;
}

bool jsonBoolArg( const std::string &obj, const std::string &key, bool &out )
{
    Kind kind; std::string val;
    if( !findMember( obj, key, kind, val ) ) return false;
    return truthy( val, out );
}

bool looksJsonObject( const std::string &obj )
{
    std::size_t i = 0;
    skipWs( obj, i );
    if( i >= obj.size() || obj[i] != '{' ) return false;
    int depth = 0;
    bool instr = false;
    for( ; i < obj.size(); ++i )
    {
        char c = obj[i];
        if( instr )
        {
            if( c == '\\' ) { ++i; continue; }
            if( c == '"' ) instr = false;
            continue;
        }
        if( c == '"' ) instr = true;
        else if( c == '{' ) ++depth;
        else if( c == '}' ) { --depth; if( depth == 0 ) return true; }
    }
    return false;
}

} // namespace atui::tools
} // namespace atui
