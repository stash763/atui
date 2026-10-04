#include "core/string.h"
#include <cstring>
#include <cstdlib>

namespace atui {
namespace core {

static const int kMinCap = 16;

String::String()
    : data_( 0 ), size_( 0 ), cap_( 0 ) {}

String::String( const char *s )
    : data_( 0 ), size_( 0 ), cap_( 0 )
{
    append( s );
}

String::String( const String &o )
    : data_( 0 ), size_( 0 ), cap_( 0 )
{
    if( o.size_ > 0 ) {
        ensure( o.size_ );
        memcpy( data_, o.data_, o.size_ );
    }
    size_ = o.size_;
}

String::~String()
{
    delete data_;
}

String &String::operator=( const String &o )
{
    if( this != &o ) {
        data_ = 0;
        size_ = 0;
        cap_ = 0;
        if( o.size_ > 0 ) {
            ensure( o.size_ );
            memcpy( data_, o.data_, o.size_ );
        }
        size_ = o.size_;
    }
    return *this;
}

int String::size() const
{
    return size_;
}

const char *String::c_str() const
{
    if( !data_ || size_ == 0 )
        return "";
    return data_;
}

void String::clear()
{
    size_ = 0;
    if( data_ )
        data_[ 0 ] = 0;
}

void String::reserve( int n )
{
    if( n > cap_ )
        ensure( n );
}

void String::append( const char *s )
{
    if( !s )
        return;
    int n = ( int )std::strlen( s );
    ensure( size_ + n + 1 );
    if( data_ && n > 0 )
        memcpy( data_ + size_, s, n );
    size_ += n;
    if( data_ && size_ > 0 )
        data_[ size_ ] = 0;
}

void String::append( const String &s )
{
    if( s.size_ == 0 )
        return;
    ensure( size_ + s.size_ + 1 );
    if( data_ && s.data_ && s.size_ > 0 )
        memcpy( data_ + size_, s.data_, s.size_ );
    size_ += s.size_;
    if( data_ && size_ > 0 )
        data_[ size_ ] = 0;
}

String &String::operator+=( const char *s )
{
    append( s );
    return *this;
}

String &String::operator+=( const String &s )
{
    append( s );
    return *this;
}

char String::at( int i ) const
{
    if( i >= 0 && i < size_ && data_ )
        return data_[ i ];
    return 0;
}

int String::indexOf( const char *needle ) const
{
    return indexOf( needle, 0 );
}

int String::indexOf( const char *needle, int from ) const
{
    if( !needle || !data_ )
        return ( needle ) ? 0 : -1;

    int n = ( int )std::strlen( needle );
    if( n == 0 )
        return ( from <= size_ ) ? from : size_;
    if( from > size_ )
        return -1;

    int i = ( from > 0 ) ? from : 0;
    while( i + n <= size_ ) {
        if( memcmp( data_ + i, needle, n ) == 0 )
            return i;
        ++i;
    }
    return -1;
}

String String::substr( int begin, int end ) const
{
    String r;
    if( begin < 0 )
        begin = 0;
    if( begin > size_ )
        return r;

    int to = size_;
    if( end >= 0 && end < to )
        to = end;
    // end == -1 means "to end" (handled as size_)

    int n = to - begin;
    if( n <= 0 )
        return r;

    r.ensure( n );
    if( data_ && r.data_ && n > 0 )
        memcpy( r.data_, data_ + begin, n );
    r.size_ = n;
    r.data_[ n ] = 0;
    return r;
}

int String::compareTo( const String &o ) const
{
    int n = ( size_ < o.size_ ) ? size_ : o.size_;
    int r = 0;
    for( int i = 0; i < n; ++i ) {
        if( data_ && o.data_ && data_[ i ] != o.data_[ i ] ) {
            r = ( data_[ i ] < o.data_[ i ] ) ? -1 : 1;
            break;
        }
    }
    if( r == 0 && size_ != o.size_ )
        r = ( size_ < o.size_ ) ? -1 : 1;
    return r;
}

bool String::equals( const String &o ) const
{
    return compareTo( o ) == 0;
}

bool String::equals( const char *o ) const
{
    if( !o )
        return size_ == 0;
    int n = ( int )std::strlen( o );
    if( size_ != n )
        return false;
    return ( n == 0 ) || ( data_ && memcmp( data_, o, n ) == 0 );
}

void String::ensure( int n )
{
    if( n <= 0 )
        return;
    int alloc = ( n < kMinCap ) ? kMinCap : n;
    if( alloc <= cap_ )
        return;

    char *nd = new char[ alloc ];
    if( !nd )
        return;

    if( data_ && size_ > 0 )
        memcpy( nd, data_, size_ );
    delete data_;

    data_ = nd;
    cap_ = alloc;
    if( data_ )
        data_[ size_ ] = 0;
}

} // namespace atui::core
} // namespace atui
