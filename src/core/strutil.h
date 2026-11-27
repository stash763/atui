// Helpers for a standard library that stops short.
//
// The rule for this file is the rule for compat/: something belongs here when
// the library the newer targets have is simply not in Watcom 1.9's.  Measured
// on the box, that is: std::string has no compare(), no front(), no back() and
// no to_string(); there is no free std::getline(); `ostream << std::string` is
// refused ("left expression must be integral"); and <sstream> is an empty file,
// so std::ostringstream does not exist at all.  Each helper below says which of
// those gaps it fills.
//
// The one helper that is not about strings, clearAll, lives here for the same
// reason: it fills a library gap, and this is where the library gaps go.  Note
// that the two era libraries are not missing the same things -- Watcom's
// containers have clear() and Borland's do not -- so what each helper stands in
// for is written with the helper rather than assumed from the file's name.
//
// Everything here is C++98 and exception-free, like the rest of core/.

#ifndef ATUI_CORE_STRUTIL_H
#define ATUI_CORE_STRUTIL_H

#include "core/int64.h"    // Int64 and UInt64: Borland 5.02 has no `long long`

#include <cstddef>
#include <string>

namespace atui {
namespace core {

// ----------------------------------------------------------------------------
// Prefix and suffix tests.
//
// std::string::compare() is the usual way to write both, and it is missing; but
// atui never needed the three-way answer -- every call site asked "== 0" -- so
// these take its place without pretending to be it.
// ----------------------------------------------------------------------------

bool startsWith( const std::string &s, const char *prefix );

// The same test at an offset inside the string, for `s.compare( at, n, p ) == 0`.
bool startsWith( const std::string &s, std::size_t at, const char *prefix );

bool endsWith( const std::string &s, const char *suffix );

// File-name suffixes.  OS/2 and DOS store names in upper case, and a file a
// person made by hand should still be recognised, so the journal's ".PRE" and
// ".INF" tests are not case sensitive.
bool endsWithIgnoreCase( const std::string &s, const char *suffix );

// A one-line title cut from longer text: the first line, control
// characters folded away, whitespace collapsed, and cut to `width`
// columns with the trailing "~" that always means "more" (a title is
// read in a list, where the whole point is that it fits). Empty text,
// or a first line of only blanks, gives an empty title: no title is
// honest, a row of spaces is not.
std::string titleFromText( const std::string &text, std::size_t width );

// ----------------------------------------------------------------------------
// Numbers to text (std::to_string is missing).
//
// Same output as the C++11 function: integers in decimal, doubles with six
// digits after the decimal point, because that is what std::to_string( double )
// produces and these strings end up in JSON request bodies and in .INF files.
// ----------------------------------------------------------------------------

std::string toString( int value );
std::string toString( long value );
std::string toString( unsigned long value );
std::string toString( double value );

// ----------------------------------------------------------------------------
// Fmt: a string being written to.
//
// Replaces std::ostringstream for the "a message, out of text and numbers"
// idiom.  It has the same shape -- chained operator<<, then str() -- so a call
// site changes in one place only.
//
// Every arithmetic type has its own operator<< on purpose.  Watcom's size_t is
// `unsigned` and its long is the same width, so an argument like `v.size()`
// would face several conversions of equal rank and the call would be ambiguous;
// with an exact match for each type nothing has to convert.
//
// Only what atui streams is here, and there is no state beyond the text and the
// precision: no fill, no width, no error flags.  A message builder either has
// the text or it does not.
// ----------------------------------------------------------------------------

class Fmt {
public:
    Fmt();

    Fmt &operator<<( const char *text );
    Fmt &operator<<( const std::string &text );
    Fmt &operator<<( char c );

    Fmt &operator<<( bool value );
    Fmt &operator<<( short value );
    Fmt &operator<<( unsigned short value );
    Fmt &operator<<( int value );
    Fmt &operator<<( unsigned int value );
    Fmt &operator<<( long value );
    Fmt &operator<<( unsigned long value );
    Fmt &operator<<( Int64 value );
    Fmt &operator<<( UInt64 value );
    Fmt &operator<<( double value );

    // std::ostream's meaning: significant digits in the general form.  Six, the
    // same default std::ostream starts with.
    Fmt &precision( int digits );

    const std::string &str() const { return s_; }
    void clear();

private:
    std::string s_;
    int digits_;
};

// ----------------------------------------------------------------------------
// Emptied and shortened containers.
//
// Borland C++ 5.02 ships Rogue Wave's 1995 standard library, which predates
// clear(): no string, no vector, no map (measured: "'clear' is not a member of
// 'std::basic_string<...>'", and the word clear does not appear in the library's
// vector.h, string.h, map.h or set.h at all).  Watcom has them, so this is a gap
// in one era library and not the other.  [container.requirements] defines clear()
// as erase( begin(), end() ) and every library here has that, so one spelling
// serves the whole tree -- strings, vectors, maps, whatever it is emptied of.
//
// The same library has erase() but only in its iterator form: erase( pos ) and
// erase( pos, len ) are "Could not find a match for ...::erase(unsigned int)"
// there.  eraseFrom and eraseAt are those two by position, which is how every
// call site in this program reads, and they are the standard's meaning: from a
// position to the end, and a position and a count.
// ----------------------------------------------------------------------------

template< class C >
inline void clearAll( C &c )
{
    c.erase( c.begin(), c.end() );
}

void eraseFrom( std::string &s, std::size_t from );

// `len` is a count, not std::string::npos: say eraseFrom when the end is meant.
void eraseAt( std::string &s, std::size_t pos, std::size_t len );

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_STRUTIL_H
