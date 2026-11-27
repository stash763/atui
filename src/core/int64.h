// 64-bit integers, on the one compiler that spells them differently.
//
// Borland C++ 5.02 has no `long long`: the phrase is "Too many types in
// declaration", and so is `1000LL` when it appears on a literal.  It has the type
// and the suffix anyway, under the names it used before the standard took them
// over -- `__int64`, `unsigned __int64`, and `I64`/`UI64` on the end of a
// constant.  Measured both ways, including the digits: `%I64u` of
// 14695981039346656037 prints 14695981039346656037, so this is a matter of
// spelling and not of the arithmetic or the CRT behind it.
//
// Which is why the seam is a typedef rather than a wrapper: there is nothing to
// implement, only a name to agree on.  Where the compiler has the portable
// spelling these are that spelling and cost nothing; where it does not, they are
// the same width, the same signedness and the same operations.
//
// The literals need a macro because a suffix cannot be typedef'd.  The one place
// that matters is the FNV-1a constants in journal.cpp, which are the offset
// basis and the prime and have to be exactly the 64-bit values the algorithm
// says -- `ATUI_U64(14695981039346656037)` is that number on both spellings,
// where the plain decimal would be a 32-bit int on Borland and quietly wrong.
//
// What this is not: <cstdint>.  There is no std::int64_t here (no <cstdint>, and
// the fixed-width names in tvision's ttypes.h stop at 32 bits for Borland), and
// atui needs exactly this one pair rather than a set of exact-width aliases it
// would have to keep honest against every era target's own headers.

#ifndef ATUI_CORE_INT64_H
#define ATUI_CORE_INT64_H

namespace atui {
namespace core {

#if defined( __BORLANDC__ )
    typedef __int64          Int64;
    typedef unsigned __int64 UInt64;
#else
    typedef long long          Int64;
    typedef unsigned long long UInt64;
#endif

} // namespace core
} // namespace atui

#if defined( __BORLANDC__ )
#  define ATUI_I64( digits )  digits##I64
#  define ATUI_U64( digits )  digits##UI64
#else
#  define ATUI_I64( digits )  digits##LL
#  define ATUI_U64( digits )  digits##ULL
#endif

#endif // ATUI_CORE_INT64_H
