/* The same format matrix through two snprintfs: glibc's and the compat one.

   compat/bcc/include/cstdio.h rewrites a printf format so that a 1997 CRT cannot
   write outside the caller's buffer, and it is the only piece of the compat tree
   that implements something rather than publishing names.  Rewriting formats is
   the kind of code that looks right and is wrong in a way that shows up as
   corrupted memory three calls later, so it is tested against the formatter this
   program is written for: one source, built by g++ and by bcc32, and the two
   outputs compared.

   What is compared is not just the text.  Every call is into a buffer surrounded
   by a canary, and the program reports three things per call -- what the buffer
   holds, what snprintf returned, and whether the canary still says the formatter
   stayed inside the size it was given.  A formatter that produces the right
   characters and writes one past the end has not passed.

   Differences between the two runs are not all bugs, and the program says which
   is which: the clamp at kMaxField is a documented deviation, and the shape of a
   %e exponent is Borland's CRT and not the rewrite.  What neither of those covers
   is a difference in the digits, the return value, or the canary.
*/
#include <cstdio>
#include <cstring>

#include "core/int64.h"

namespace {

const char kCanary = '~';
const std::size_t kRaw = 4200;   /* more than kMaxField, to watch the clamp */
const std::size_t kReport = 96;

/* The buffer plus the room that must come back untouched. */
struct Probe {
    char raw[kRaw];
};

void reset( Probe &p )
{
    std::memset( p.raw, kCanary, kRaw );
}

/* What the buffer holds, with anything that is not printable shown as a number,
   so a stray NUL or 0x01 is visible rather than invisible in the diff. */
void describe( const Probe &p, std::size_t size, char *out )
{
    std::size_t o = 0;
    const std::size_t look = size < kReport ? size : kReport;
    for ( std::size_t i = 0; i < look && p.raw[i]; ++i ) {
        const unsigned char c = (unsigned char) p.raw[i];
        if ( c >= 32 && c < 127 )
            out[ o++ ] = (char) c;
        else {
            std::sprintf( out + o, "\\x%02x", (unsigned) c );
            o += 4;
        }
    }
    out[ o ] = '\0';
}

int failures = 0;

void check( const char *name, std::size_t size, int rc, const Probe &p )
{
    char shown[ kReport * 5 ];
    describe( p, size, shown );

    bool intact = true;
    for ( std::size_t i = size; i < kRaw; ++i )
        if ( p.raw[ i ] != kCanary )
            intact = false;

    // n == 0 means the standard asks for nothing at all to be written, so the
    // test there is that nothing was, not that something was terminated.
    const bool terminated = 0 == size
        ? ( p.raw[ 0 ] == kCanary )
        : ( std::strlen( p.raw ) < size );

    if ( !intact )
        ++failures;
    if ( !terminated )
        ++failures;

    std::printf( "%-26s n=%-3u rc=%-6d %s%s|%s|\n",
                 name, (unsigned) size, rc,
                 intact ? " " : "WROTE-PAST-SIZE ",
                 terminated ? "" : "NOT-TERMINATED ",
                 shown );
}

#define CASE0( name, size, fmt ) \
    { Probe p; reset( p ); check( name, size, std::snprintf( p.raw, size, fmt ), p ); } while ( 0 )
#define CASE1( name, size, fmt, a1 ) \
    { Probe p; reset( p ); check( name, size, std::snprintf( p.raw, size, fmt, a1 ), p ); } while ( 0 )
#define CASE2( name, size, fmt, a1, a2 ) \
    { Probe p; reset( p ); check( name, size, std::snprintf( p.raw, size, fmt, a1, a2 ), p ); } while ( 0 )
#define CASE3( name, size, fmt, a1, a2, a3 ) \
    { Probe p; reset( p ); check( name, size, std::snprintf( p.raw, size, fmt, a1, a2, a3 ), p ); } while ( 0 )
#define CASE4( name, size, fmt, a1, a2, a3, a4 ) \
    { Probe p; reset( p ); check( name, size, std::snprintf( p.raw, size, fmt, a1, a2, a3, a4 ), p ); } while ( 0 )
#define CASE5( name, size, fmt, a1, a2, a3, a4, a5 ) \
    { Probe p; reset( p ); check( name, size, std::snprintf( p.raw, size, fmt, a1, a2, a3, a4, a5 ), p ); } while ( 0 )
#define CASE6( name, size, fmt, a1, a2, a3, a4, a5, a6 ) \
    { Probe p; reset( p ); check( name, size, std::snprintf( p.raw, size, fmt, a1, a2, a3, a4, a5, a6 ), p ); } while ( 0 )

const char *kWord = "hello";
const char *kLong = "0123456789012345678901234567890123456789";

} // namespace

int main()
{
    /* So the last line printed names the case a fault happened in. */
    std::setvbuf( stdout, 0, _IONBF, 0 );

    /* numbers, and the flags that go with them */
    CASE1(  "int", 16, "%d", 42  );
    CASE1(  "int-negative", 16, "%d", -42  );
    CASE1(  "int-width", 16, "%8d|", 42  );
    CASE1(  "int-left", 16, "%-8d|", 42  );
    CASE1(  "int-plus", 16, "%+d", 42  );
    CASE1(  "int-space", 16, "% d", 42  );
    CASE1(  "int-zero-pad", 16, "%08d", 42  );
    CASE1(  "unsigned", 16, "%u", 4000000000u  );
    CASE1(  "hex", 16, "%x", 255  );
    CASE1(  "hex-alt", 16, "%#x", 255  );
    CASE1(  "hex-upper", 16, "%08X", 255  );
    CASE1(  "octal", 16, "%o", 8  );
    CASE1(  "octal-alt", 16, "%#o", 8  );

    /* strings */
    CASE1(  "string", 16, "%s", kWord  );
    CASE1(  "string-width", 16, "%10s|", kWord  );
    CASE1(  "string-left", 16, "%-10s|", kWord  );
    CASE1(  "string-prec", 16, "%.3s", kWord  );
    CASE1(  "string-both", 16, "%8.3s|", kWord  );
    CASE1(  "string-empty", 16, "[%s]", ""  );
    CASE1(  "string-long", 16, "%s", kLong  );
    CASE1(  "string-long-small-buffer", 4, "%s", kLong  );

    /* characters, per cent, and text with no conversion in it */
    CASE1(  "char", 16, "%c", 'A'  );
    CASE0(  "percent", 16, "100%%"  );
    CASE0(  "plain-text", 16, "no conversions here"  );
    CASE0(  "empty-format", 16, ""  );

    /* floating point -- atui prints %.*g for a double */
    CASE1(  "double-f", 24, "%f", 1.5  );
    CASE1(  "double-prec", 24, "%.2f", 1.0 / 3.0  );
    CASE1(  "double-width", 24, "%10.3f|", 1.0 / 3.0  );
    CASE1(  "double-e", 32, "%e", 1.0  );
    CASE1(  "double-E", 32, "%E", 123456.789  );
    CASE1(  "double-g", 32, "%g", 1.0 / 3.0  );
    CASE2(  "double-star-prec", 32, "%.*g", 6, 1.0 / 3.0  );
    CASE2(  "double-star-prec-2", 32, "%.*f", 3, 12.5  );
    CASE1(  "double-zero-prec", 32, "%.0f", 2.5  );
    CASE1(  "double-negative-zero", 32, "%f", -0.0  );
    CASE1(  "double-big", 32, "%g", 1e100  );

    /* the starred forms, including left-adjusted */
    CASE2(  "star-width", 16, "%*d", 6, 42  );
    CASE2(  "star-left", 16, "%-*d|", 6, 42  );
    CASE4(  "star-two", 24, "[%*d][%.*f]", 4, 7, 2, 1.0 / 3.0  );

    /* 64-bit integers: the compat header rewrites %ll to Borland's %I64 */
    CASE1(  "i64", 32, "%lld", (atui::core::Int64) ATUI_I64( -1234567890123 )  );
    CASE1(  "u64", 32, "%llu", (atui::core::UInt64) ATUI_U64( 14695981039346656037 )  );
    CASE1(  "u64-padded", 32, "%016llx", (atui::core::UInt64) ATUI_U64( 81985529216486895 )  );
    CASE1(  "i64-width", 32, "%25lld", (atui::core::Int64) -1  );

    /* the length modifiers the compat header drops because the types do not exist
       on this target: size_t is an int here, so the digits must still agree */
    CASE1(  "z-unsigned", 24, "%zu", (std::size_t) 12345  );
    CASE1(  "z-hex", 24, "%zx", (std::size_t) 4096  );
    CASE1(  "l-long", 24, "%ld", (long) -2000000000L  );
    CASE1(  "h-short", 24, "%hd", (int) (short) -5  );

    /* several conversions in one format, which is where an argument list that is
       one conversion out of step shows up */
    CASE6(  "mixed", 64, "%s=%d/%d %.2f %c %x", "ratio", 3, 4, 0.75, 'z', 65535  );
    CASE4(  "mixed-tight-buffer", 12, "%s=%d/%d %.2f", "ratio", 3, 4, 0.75  );

    /* the sizes that are the whole point of a bounded formatter: nothing at all,
       one byte, and the boundary just before and after the text fits */
    CASE1(  "size-1", 1, "%d", 42  );
    CASE1(  "size-2", 2, "%d", 42  );
    CASE1(  "size-3", 3, "%d", 42  );
    CASE1(  "size-exact", 5, "%d", 42  );
    CASE1(  "size-one-short", 4, "%d", 42  );
    CASE1(  "size-0", 0, "%d", 42  );

    /* beyond the clamp, which is a documented deviation from glibc and is here so
       that the deviation is a measurement and not a claim: kMaxField is 1024, so
       a 3000-character string is cut short and the return value says the short
       number rather than the long one */
    {
        static char big[ 3001 ];
        std::memset( big, 'x', 3000 );
        big[ 3000 ] = '\0';
        Probe p;
        reset( p );
        const int rc = std::snprintf( p.raw, 20, "%s", big );
        char shown[ 32 ];
        describe( p, 20, shown );
        std::printf( "%-26s n=%-3u rc=%-6d (clamp: glibc counts 3000) |%s|\n",
                     "string-past-clamp", (unsigned) 20, rc, shown );
        Probe q;
        reset( q );
        const int rc2 = std::snprintf( q.raw, 40, "%.*s", 3000, big );
        describe( q, 40, shown );
        std::printf( "%-26s n=%-3u rc=%-6d (clamp: star)          |%s|\n",
                     "star-precision-past-clamp", (unsigned) 40, rc2, shown );
    }

    /* The clamp the rewrite does apply, measured rather than asserted: a literal
       width or precision above kMaxField is rewritten as kMaxField, so this is
       the one place the era build and every other build are allowed to disagree,
       and the only way to know is to ask both. */
    {
        static char big[ 3001 ];
        std::memset( big, 'x', 3000 );
        big[ 3000 ] = '\0';

        const std::size_t n = 2100;
        Probe p;
        reset( p );
        check( "literal-prec-2000", n, std::snprintf( p.raw, n, "%.2000s", big ), p );

        Probe q;
        reset( q );
        check( "literal-width-2000", n, std::snprintf( q.raw, n, "%2000d", 7 ), q );

        Probe r;
        reset( r );
        check( "literal-both-1500-4", n, std::snprintf( r.raw, n, "%1500.4f", 0.5 ), r );

        /* and a `*` at the largest value the compat header accepts, which is the
           case its sizing has to survive: the room is the caller's number */
        Probe t;
        reset( t );
        check( "star-width-4096", n, std::snprintf( t.raw, n, "%*d", 4096, 7 ), t );
    }

    /* A `*` bigger than the compat header will accept: it refuses, which is a
       deviation from a formatter that simply prints, and the refusal is only
       worth having if the buffer is left in the state the standard says a failed
       call leaves it -- empty and terminated -- which is what these two cases
       check on both builds. */
    {
        Probe p;
        reset( p );
        check( "star-width-4097", 64, std::snprintf( p.raw, 64, "%*d", 4097, 7 ), p );

        Probe q;
        reset( q );
        check( "star-width-65536", 64, std::snprintf( q.raw, 64, "%*d", 65536, 7 ), q );
    }

    std::printf( "\ncalls that wrote outside the size they were given: %d\n",
                 failures );
    return failures ? 1 : 0;
}
