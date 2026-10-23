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
        std::memcpy( data_, o.data_, o.size_ );
        size_ = o.size_;
        /* Terminated here rather than left to ensure(), which had already run and
           put its NUL at data_[0] while the string was still empty -- where the
           memcpy above then wrote over it.  c_str() hands back this block to
           printf and strcmp, so an unterminated copy is not a cosmetic defect: it
           is a read past the allocation the first time anybody prints the string. */
        data_[ size_ ] = 0;
    }
}

String::~String()
{
    // delete[] , because ensure grew it with new char[].
    delete[] data_;
}

String &String::operator=( const String &o )
{
    if( this != &o ) {
        /* clear() keeps the block and resets the length; setting data_ to zero
           instead lost the block, so every assignment leaked whatever the string
           had been holding. */
        clear();
        if( o.size_ > 0 ) {
            ensure( o.size_ );
            std::memcpy( data_, o.data_, o.size_ );
            size_ = o.size_;
            data_[ size_ ] = 0;
        }
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
        std::memcpy( data_ + size_, s, n );
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
        std::memcpy( data_ + size_, s.data_, s.size_ );
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
    /* Both questions used to be answered with 0 -- a null needle, and a needle in
       an empty string -- and 0 is what a caller reads as "found it here".  A null
       needle is not found; an empty string holds no needle that is not empty. */
    if( !needle )
        return -1;

    int n = ( int )std::strlen( needle );
    if( n == 0 )
        return ( from <= size_ ) ? ( ( from > 0 ) ? from : 0 ) : size_;
    if( !data_ || from > size_ )
        return -1;

    int i = ( from > 0 ) ? from : 0;
    while( i + n <= size_ ) {
        if( std::memcmp( data_ + i, needle, n ) == 0 )
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
        std::memcpy( r.data_, data_ + begin, n );
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
    return ( n == 0 ) || ( data_ && std::memcmp( data_, o, n ) == 0 );
}

void String::ensure( int n )
{
    if( n <= 0 )
        return;

    /* The invariant, which every writer in this file depends on and one of them
       once did not: cap_ is the bytes of storage held, and a string of cap_-1
       characters still needs one byte for the terminator.  So the allocation is
       one more than the caller asked for.  Without that, a caller asking for room
       for n characters cannot put the NUL at data_[n] -- substr did exactly that
       and wrote one byte past the block for any text longer than kMinCap, where
       the minimum capacity happened to hide it. */
    int need = n + 1;
    int alloc = ( need < kMinCap ) ? kMinCap : need;
    if( alloc <= cap_ )
        return;

    char *nd = new char[ alloc ];

    if( data_ && size_ > 0 )
        std::memcpy( nd, data_, size_ );
    delete[] data_;

    data_ = nd;
    cap_ = alloc;
    data_[ size_ ] = 0;
}

} // namespace atui::core
} // namespace atui
