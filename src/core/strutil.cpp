// See strutil.h for why each of these exists.

#include "core/strutil.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace atui {
namespace core {

namespace {

// std::tolower takes an int that is either unsigned char or EOF, and the C
// library is allowed to be a table indexed by that int.
char lowerOf( char c )
{
    return static_cast<char>(
        std::tolower( static_cast<int>( static_cast<unsigned char>( c ) ) ) );
}

// Writes into `buf`, which every caller here makes generously large; snprintf
// truncates rather than overflowing, so a surprise value costs a short string
// and not the stack.  Each helper passes its argument with the type its
// conversion asks for -- the C library cannot check that for us.

} // anonymous namespace

bool startsWith( const std::string &s, const char *prefix )
{
    return startsWith( s, 0, prefix );
}

bool startsWith( const std::string &s, std::size_t at, const char *prefix )
{
    if( 0 == prefix )
        return false;
    const std::size_t n = std::strlen( prefix );
    if( at > s.size() || s.size() - at < n )
        return false;
    return 0 == std::memcmp( s.data() + at, prefix, n );
}

bool endsWith( const std::string &s, const char *suffix )
{
    if( 0 == suffix )
        return false;
    const std::size_t n = std::strlen( suffix );
    if( s.size() < n )
        return false;
    return 0 == std::memcmp( s.data() + ( s.size() - n ), suffix, n );
}

bool endsWithIgnoreCase( const std::string &s, const char *suffix )
{
    if( 0 == suffix )
        return false;
    const std::size_t n = std::strlen( suffix );
    if( s.size() < n )
        return false;
    const std::size_t at = s.size() - n;
    for( std::size_t i = 0; i < n; ++i )
        if( lowerOf( s[at + i] ) != lowerOf( suffix[i] ) )
            return false;
    return true;
}

std::string titleFromText( const std::string &text, std::size_t width )
{
    if( width == 0 )
        return std::string();

    // The first line, folded flat: everything a title is cut from.
    // (A loop running `i != nl` is the trap -- no newline means npos,
    // and npos is not the end of the string.  The segment is taken by
    // name before it is walked.)
    const std::string::size_type nl = text.find( '\n' );
    const std::string line = ( nl == std::string::npos )
        ? text
        : text.substr( 0, nl );
    std::string flat;
    for( std::string::size_type i = 0; i != line.size(); ++i )
    {
        char c = line[ i ];
        if( (unsigned char)c < ' ' )
            c = ' ';              // tabs and other controls read as space
        if( c == ' ' )
        {
            if( !flat.empty() && flat[ flat.size() - 1 ] != ' ' )
                flat += ' ';
        }
        else
            flat += c;
    }
    while( !flat.empty() && flat[ flat.size() - 1 ] == ' ' )
        flat.erase( flat.size() - 1 );

    if( flat.size() <= width )
        return flat;
    if( width == 1 )
        return "~";
    std::string cut = flat.substr( 0, width - 1 );
    while( !cut.empty() && cut[ cut.size() - 1 ] == ' ' )
        cut.erase( cut.size() - 1 );
    return cut + "~";
}

std::string toString( int value )
{
    char buf[24];
    std::snprintf( buf, sizeof buf, "%d", value );
    return std::string( buf );
}

std::string toString( long value )
{
    char buf[24];
    std::snprintf( buf, sizeof buf, "%ld", value );
    return std::string( buf );
}

std::string toString( unsigned long value )
{
    char buf[24];
    std::snprintf( buf, sizeof buf, "%lu", value );
    return std::string( buf );
}

std::string toString( double value )
{
    // "%f" of a double can run to three hundred-odd digits; atui's doubles are
    // temperatures and time spans, but the buffer is the size the format needs
    // so that a value from a settings file cannot be cut short.
    char buf[384];
    std::snprintf( buf, sizeof buf, "%f", value );
    return std::string( buf );
}

// ----------------------------------------------------------------------------
// Fmt
// ----------------------------------------------------------------------------

Fmt::Fmt()
    : s_(), digits_( 6 )
{
}

Fmt &Fmt::operator<<( const char *text )
{
    if( 0 != text )
        s_ += text;
    return *this;
}

Fmt &Fmt::operator<<( const std::string &text )
{
    s_ += text;
    return *this;
}

Fmt &Fmt::operator<<( char c )
{
    s_ += c;
    return *this;
}

Fmt &Fmt::operator<<( bool value )
{
    // As std::ostream without boolalpha: 1 or 0.
    s_ += ( value ? "1" : "0" );
    return *this;
}

Fmt &Fmt::operator<<( short value )
{
    char buf[16];
    std::snprintf( buf, sizeof buf, "%hd", value );
    s_ += buf;
    return *this;
}

Fmt &Fmt::operator<<( unsigned short value )
{
    char buf[16];
    std::snprintf( buf, sizeof buf, "%hu", value );
    s_ += buf;
    return *this;
}

Fmt &Fmt::operator<<( int value )
{
    char buf[24];
    std::snprintf( buf, sizeof buf, "%d", value );
    s_ += buf;
    return *this;
}

Fmt &Fmt::operator<<( unsigned int value )
{
    char buf[24];
    std::snprintf( buf, sizeof buf, "%u", value );
    s_ += buf;
    return *this;
}

Fmt &Fmt::operator<<( long value )
{
    char buf[24];
    std::snprintf( buf, sizeof buf, "%ld", value );
    s_ += buf;
    return *this;
}

Fmt &Fmt::operator<<( unsigned long value )
{
    char buf[24];
    std::snprintf( buf, sizeof buf, "%lu", value );
    s_ += buf;
    return *this;
}

Fmt &Fmt::operator<<( long long value )
{
    char buf[32];
    std::snprintf( buf, sizeof buf, "%lld", value );
    s_ += buf;
    return *this;
}

Fmt &Fmt::operator<<( unsigned long long value )
{
    char buf[32];
    std::snprintf( buf, sizeof buf, "%llu", value );
    s_ += buf;
    return *this;
}

Fmt &Fmt::operator<<( double value )
{
    char buf[384];
    std::snprintf( buf, sizeof buf, "%.*g", digits_, value );
    s_ += buf;
    return *this;
}

Fmt &Fmt::precision( int digits )
{
    digits_ = ( digits < 1 ) ? 1 : digits;
    return *this;
}

void Fmt::clear()
{
    s_.clear();
}

} // namespace atui::core
} // namespace atui
