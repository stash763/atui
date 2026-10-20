// What does Watcom 1.9's std::map allow through a const reference?
//
// atui's lookup helpers (src/core/lookup.h) walk a const map by iterator because
// the library does not answer the lookups a modern library would.  This asks each
// spelling separately in one compile -- the compiler reports a bad member per
// function and carries on -- with probe_bad() as a control: a run that named only
// `count`, `find` and `no_such_member` proves the file was read and that the
// errors are about the library rather than about this program.
//
//     build.cmd one probe_constmap.cpp
//
// Measured on the box, 2026-10-20, Open Watcom 1.9:
//
//   E419  cannot call non-const function for a constant object   -- find(), line A
//   E715  member 'count' has not been declared                   -- count(), line B
//   E715  member 'no_such_member' has not been declared          -- control, line C
//
// i.e. `find()` is non-const only and `count()` does not exist.  `<map>` in
// D:\WATCOM\H prints an interface listing that claims otherwise, but the listing
// is inside a /* */ block and mis-types const_iterator, so it is documentation
// rather than code.  Function D below (find through a non-const reference) is the
// form that does compile, and is in the file to keep the other three in context.
// This file therefore never compiles: it is a measurement, not a test.  Not built
// by CMake and not shipped.

#include <map>
#include <string>

// A: the question atui's lookup helper was written for.
int probe_const_find( const std::map< std::string, int > &m, const std::string &k )
{
    std::map< std::string, int >::const_iterator it = m.find( k );
    return it == m.end() ? 0 : it->second;
}

// B: count(), the other const lookup the standard offers.
int probe_const_count( const std::map< std::string, int > &m, const std::string &k )
{
    return (int) m.count( k );
}

// C: control -- a member that certainly is not there.
int probe_bad( const std::map< std::string, int > &m, const std::string &k )
{
    return (int) m.no_such_member( k );
}

// D: the non-const spelling, which atui does use (judge.cpp's plan cache).
int probe_mutable_find( std::map< std::string, int > &m, const std::string &k )
{
    std::map< std::string, int >::iterator it = m.find( k );
    return it == m.end() ? 0 : it->second;
}

int main()
{
    std::map< std::string, int > m;
    return probe_const_find( m, "a" ) + probe_const_count( m, "b" )
         + probe_bad( m, "c" ) + probe_mutable_find( m, "d" );
}
