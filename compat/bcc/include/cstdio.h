/* cstdio -- <stdio.h> in namespace std, and the function the CRT does not have.

   Two separate jobs, which is why this header is longer than its siblings.

   The easy one: Borland C++ 5.02 has no <cstdio> at all ("Unable to open include
   file 'cstdio.h'", measured; twenty of atui's files ask for it) and its
   <stdio.h> declares nothing in namespace std (measured: std::printf is answered
   with "Qualifier 'std' is not a class or namespace name").  Every name at the
   bottom of this file is published with a using-declaration, which this compiler
   accepts for a function as readily as for a typedef -- each line was compiled,
   not assumed.

   The hard one: this CRT has no snprintf.  Not hidden and not spelled oddly --
   a grep of the whole include tree finds no snprintf, no _snprintf and no
   _vsnprintf, and the only formatter is vsprintf, which takes no length.  atui
   calls std::snprintf 32 times, always into a buffer whose size it passes, so the
   era build needs a bounded formatter and there is nothing here to build one out
   of.

   Passing the caller's format straight to vsprintf would be wrong in both
   directions: it writes wherever the conversion tells it to, past the caller's
   buffer among other places.  So the format is rewritten first, and the rewrite
   is what makes the output bounded:

     * every literal field width and precision is clamped to kMaxField,
     * a `%s` is bounded not by a number invented here but by the string itself,
       which is measured while the arguments are walked for exactly this purpose,
     * a `*` width or precision is copied rather than replaced, because the value
       it names is an argument that stays in the caller's list: reading it is why
       the walk reads arguments at all, and why it reads them from a copy of the
       va_list rather than from the one the print below uses.  A `*` is the one
       number that cannot be clamped, because the format goes on asking for it and
       the print then takes the room the caller's value says it takes: its value
       sizes the buffer, and anything above kMaxStar is refused.  atui writes
       `%.*g` for a double and uses no other starred field.

   With that, the worst case for each conversion is known, the sum of the worst
   cases (plus the strings it measured) is what gets allocated, and vsprintf
   receives a format that cannot produce more.  The literal clamp is a deviation
   and it is visible -- `%.2000s` prints 1024 characters here and 2000 elsewhere,
   measured both ways in tools/bcc/snprintf_matrix.cpp -- but atui's 32 snprintf
   calls format numbers into buffers of 16 to 64 bytes and not one of them has a
   `%s`, so nothing in this program comes near the limit.

   The rewrite's other job is spelling 64-bit integers Borland's way: %lld becomes
   %I64d and %016llx becomes %016I64x.  Measured both ways -- this CRT prints
   %I64d, %I64u and %I64x correctly for values too big for 32 bits
   (14695981039346656037 came out as 14695981039346656037), so the digits are the
   CRT's and only the length modifier is translated.  The z, t and j modifiers are
   dropped rather than translated: on this target size_t is an int, and the types
   they name do not exist.

   Not implemented, because nothing here asks for it: %Lf, whose argument would be
   read at the wrong width.  A format longer than kMaxFormat is refused rather
   than truncated -- an empty string and -1, which is a lost message rather than
   corrupted memory.

   What it is measured against: the format matrix in tools/bcc/snprintf_matrix.cpp
   is one program built by g++ and by bcc32, and every case in it compares the
   buffer, the return value, and a canary that says whether the formatter stayed
   inside the size it was given.  58 of the 64 cases agree character for
   character, including the `%.*g` atui uses, the `%ll` to `%I64` translation, the
   dropped `z`, the truncations, the sizes 0 through exact, and a `*` width of
   4096.  The other six are the three known deviations and nothing else:

     * `%.2000s` gives 1024 characters here and 2000 on a modern CRT, `%2000d`
       gives 1024 rather than 2000, and `%1500.4f` gives 1024 rather than 1500.
       That is the literal clamp above, and it is why the clamp is a number in a
       constant rather than a rule written per case.
     * a `*` width above kMaxStar is refused -- -1 and an empty buffer -- where a
       modern formatter prints it.  Refusing is the choice made here on purpose:
       the alternative is a temporary whose size is picked by a number out of the
       caller's argument list.
     * Borland's vsprintf prints negative zero as "0.000000" where glibc prints
       "-0.000000".  That is the CRT's arithmetic from 1997 and there is nothing
       in a formatter to fix it with, so it is written down: a number that is
       exactly zero and negative loses its sign on an era target, and nothing atui
       prints is one.
*/
#ifndef ATUI_BCC_COMPAT_CSTDIO_H
#define ATUI_BCC_COMPAT_CSTDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace atui_bcc {

enum {
    kMaxField  = 1024,    /* widest field, longest string, most digits */
    kMaxFormat = 1024,    /* longest format string accepted */
    kStackTemp = 1024,    /* characters formatted without allocating */
    kMaxRoom   = 1L << 24,  /* most a formatted string may ask for: 16 MB */
    kDigits64  = 22,      /* the longest a 64-bit integer can be */
    kMaxStar   = 4096     /* largest value a `*` is asked for and accepted */
};

inline bool isFlag( char c )
{
    return '-' == c || '+' == c || ' ' == c || '#' == c || '0' == c;
}

/* One conversion, read far enough to bound it and rewrite it. */
class Spec {
public:
    Spec()
        : end( 0 ), conv( '\0' ), width( 0 ), prec( -1 ), strLen( 0 ), wide( false ),
          starWidth( false ), starPrec( false ), tooBig( false )
    {
        flags[0] = '\0';
        mod[0] = '\0';
    }

    /* va_list by reference throughout: a `*` is consumed from the walk, and a
       walk passed by value is a walk that never moves forward.  Measured on
       this compiler: `va_list walk = args` copies, va_arg advances the copy and
       leaves the original alone (which is the property the print below relies
       on), and a function that takes va_list **by value** consumes its own
       argument and nothing else -- which is how a `%.*s` in a format with more
       than one conversion sized its buffer from the wrong value. */
    void readFrom( const char *&p, va_list &args );

    const char *end;            /* just past the conversion character */
    char        flags[8];       /* as written, in the order written */
    char        mod[4];         /* the modifier to emit: "I64", "", "l", ... */
    char        conv;
    long        width;          /* literal: clamped.  star: as supplied */
    long        prec;           /* ditto; -1 when the format gives none */
    long        strLen;         /* a %s argument, as long as the caller's string */
    bool        wide;           /* the value is a 64-bit integer */
    bool        starWidth;      /* the width came from a `*` and stays one */
    bool        starPrec;       /* the precision came from a `*` and stays one */
    bool        tooBig;         /* a `*` asks for more than kMaxStar */

private:
    Spec( const Spec & );
    Spec &operator=( const Spec & );

    long numberOrStar( const char *&p, va_list &args, bool &star )
    {
        long v = 0;
        star = ( '*' == *p );
        if ( star ) {
            /* Exactly as supplied, and that is not a comment about style: the `*`
               stays in the rewritten format (the note at the rewrite says why), so
               the print takes the room the caller's number says it takes, and a
               clamp here would size the temporary smaller than what goes into it.
               This line was once `v = min(v, kMaxField)`, and the format matrix
               against glibc came back with a page fault at 0x7878787C -- four 'x'
               bytes, the string that `%.*s` of 3000 had just written over the
               stack.  Measured on this CRT: `%.*s` of 4000 writes 4000 and returns
               4000, and a negative width is used as its magnitude.  A number above
               kMaxStar is refused rather than attempted. */
            v = va_arg( args, int );
            if ( v > (long) kMaxStar )
                tooBig = true;
            ++p;
            return v;
        }

        /* Saturating rather than exact: the value is only ever wanted up to
           kMaxField, and a format that says 999999999 must not overflow the long
           on its way to being clamped. */
        while ( *p >= '0' && *p <= '9' ) {
            if ( v <= kMaxField )
                v = v * 10 + ( *p - '0' );
            ++p;
        }
        if ( v > kMaxField )
            v = kMaxField;
        return v;
    }
};

inline void Spec::readFrom( const char *&p, va_list &args )
{
    size_t n = 0;
    ++p;                                            /* the '%' */
    while ( isFlag( *p ) && n + 1 < sizeof flags )
        flags[ n++ ] = *p++;
    flags[ n ] = '\0';

    width = numberOrStar( p, args, starWidth );

    if ( '.' == *p ) {
        ++p;
        prec = numberOrStar( p, args, starPrec );
    }

    /* The length modifier, and what becomes of it.  I64 is already Borland's
       spelling.  ll is the portable one and becomes I64.  h, hh, l and L mean
       the same here as everywhere, so they are kept.  z, t and j name types this
       target does not have, and the argument they accompany is the width of an
       int anyway, so they are dropped. */
    if ( 'l' == p[0] && 'l' == p[1] )
        { ::strcpy( mod, "I64" ); wide = true; p += 2; }
    else if ( 'I' == p[0] && '6' == p[1] && '4' == p[2] )
        { mod[0] = '\0'; wide = true; p += 3; }
    else if ( 'z' == p[0] || 't' == p[0] || 'j' == p[0] )
        { mod[0] = '\0'; ++p; }
    else if ( 'h' == p[0] )
        { ::strcpy( mod, "h" ); if ( 'h' == p[1] ) ++p; ++p; }
    else if ( 'l' == p[0] )
        { ::strcpy( mod, "l" ); ++p; }
    else if ( 'L' == p[0] )
        { ::strcpy( mod, "L" ); ++p; }

    conv = *p;
    if ( conv )
        ++p;
    end = p;

    /* Past the value, whatever its width, so that the next conversion starts at
       the right argument.  int, long, unsigned and a pointer are all four bytes
       here; a variadic float arrives as a double. */
    switch ( conv ) {
        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X':
            if ( wide ) (void) va_arg( args, __int64 );
            else        (void) va_arg( args, int );
            break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
            (void) va_arg( args, double );
            break;
        case 'c': case 'p': case 'n':
            (void) va_arg( args, int );
            break;
        case 's': {
            /* Read rather than skipped: the room this conversion needs is the
               room the string has, and this is the one moment the walk is holding
               the pointer.  A null here is the caller's bug and the CRT's to
               answer -- Borland prints "(null)" -- so the room allows for that
               and nothing is dereferenced. */
            const char *v = va_arg( args, const char * );
            strLen = v ? (long) ::strlen( v ) : 8;
            break;
        }
        default:
            break;
    }
}

/* The most characters this conversion can produce.  Everything in here is a
   worst case of what the CRT will write, which is the only thing the temporary
   in vsnprintf needs to know: the format it is handed is the one being run, so
   a room that does not cover it is a buffer overflow with the caller's data in
   it. */
inline long roomFor( const Spec &s )
{
    long r = 8;
    /* A negative width means left-adjust and its magnitude (measured: `%*d` of -6
       and 42 is "42    "), so it is the magnitude that sizes the room. */
    const long prec = s.prec > 0 ? s.prec : 0;
    const long wide = s.width >= 0 ? s.width : -s.width;

    switch ( s.conv ) {
        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X':
            r = prec + kDigits64 + 8;
            break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
            /* %f of the largest double is 309 digits before the point; the
               exponent of %e is a handful more; the precision is the caller's. */
            r = 320 + ( s.prec >= 0 ? prec : 6 ) + 8;
            break;
        case 's':
            /* The string was measured while the arguments were being walked, so
               the room is the string rather than a guess at it.  Three cases, and
               the middle one is the trap: a `*` precision that arrived negative
               is ignored by the CRT -- "as if it were absent", C99 7.19.6.1, and
               measured to be exactly that on this CRT and on glibc -- so what
               goes out is the whole string however long it is, and the only bound
               on it is the string. */
            if ( s.starPrec && s.prec < 0 )
                r = s.strLen + 2;
            else if ( s.prec >= 0 )
                r = ( s.prec < s.strLen ? s.prec : s.strLen ) + 2;
            else
                r = s.strLen + 2;   /* no precision in the format: the string */
            break;
        case 'c':  r = 2;   break;
        case 'p':  r = 16;  break;
        case 'n':  r = 0;   break;
        case '%':  r = 1;   break;
        default:   r = 8;   break;
    }

    if ( wide > r )
        r = wide;
    return r;
}

inline void emit( char *out, size_t size, size_t &at, const char *text )
{
    for ( size_t i = 0; text[ i ]; ++i ) {
        if ( at + 1 >= size )
            return;
        out[ at++ ] = text[ i ];
    }
}

inline void emitNumber( char *out, size_t size, size_t &at, long v )
{
    char b[ 16 ];
    size_t i = 0;
    if ( v <= 0 )
        return;                                 /* nothing to write for no width */
    do {
        b[ i++ ] = (char) ( '0' + ( v % 10 ) );
        v /= 10;
    } while ( v && i < sizeof b );

    while ( i ) {
        if ( at + 1 >= size )
            return;
        out[ at++ ] = b[ --i ];
    }
}

} // namespace atui_bcc

namespace std {

/* The bounded formatter, and the only part of this header with detail in it:
   everything else is a list of names. */
inline int vsnprintf( char *buf, size_t n, const char *fmt, va_list args )
{
    using namespace atui_bcc;

    /* A null buffer or a null format is not something to format.  A size of zero
       is, and it has a meaning: count, write nothing.  Measured on both CRTs --
       glibc answers 2 for `%d` of 42 with n = 0, and Borland's vsprintf answers
       the same when it is given the chance -- while this function answered -1,
       which is a caller's test for failure reading a good print as a bad one. */
    if ( !buf || !fmt )
        return -1;

    if ( n > 0 )
        buf[0] = '\0';

    /* Walking the format consumes arguments, and the print below has to start
       them again, so the walk uses its own cursor.  Borland's va_list is a
       `void _FAR *` (stdarg.h line 28), so this assignment is a copy -- measured,
       because an va_list that aliased the caller's would put every number in the
       program one conversion out of place. */
    va_list walk = args;

    char   xlat[ kMaxFormat ];
    size_t at   = 0;
    long   room = 1;                        /* one for the NUL */
    bool   refuse = false;                  /* a `*` that asks for too much */

    const char *p = fmt;
    while ( *p ) {
        if ( '%' != *p ) {
            if ( at + 1 >= sizeof xlat )
                return -1;
            xlat[ at++ ] = *p++;
            ++room;
            continue;
        }

        Spec s;
        s.readFrom( p, walk );
        if ( s.tooBig )
            refuse = true;
        /* Bounded against a number rather than left to add up: the room is now the
           strings in the argument list as much as the widths in the format, and a
           `long` that runs past its end comes back small, which is the one way
           this sum could still hand vsprintf a buffer that is too tight. */
        if ( room > kMaxRoom - roomFor( s ) )
            refuse = true;
        room += roomFor( s );

        if ( at + 2 >= sizeof xlat )
            return -1;
        xlat[ at++ ] = '%';
        emit( xlat, sizeof xlat, at, s.flags );
        if ( '%' != s.conv ) {
            /* A `*` is copied, not replaced.  Replacing it with its value is what a
               formatter usually does and it looks obvious here, but arguments are
               positional: the value the caller passed for the width sits in the
               list ahead of the value being printed, and a format that stops
               asking for it hands every later conversion the argument before its
               own.  Measured: `%.*g` of 6 and 1/3 rewritten to `%.6g` printed
               1.19453e+103, which is the 6 taken as part of a double.  Copying the
               `*` leaves the list alone and leaves a negative width or precision to
               mean what the CRT says, which is the standard's meaning.  The price
               is that a `*` is not clamped, so its value sizes the room below and
               an out-of-range one is refused. */
            if ( s.starWidth )
                emit( xlat, sizeof xlat, at, "*" );
            else
                emitNumber( xlat, sizeof xlat, at, s.width );
            if ( s.starPrec || s.prec >= 0 ) {
                /* A `*` is emitted whatever its value, including negative: the
                   argument has been passed and the CRT will read it, so dropping
                   the `.*` because the number is not a positive one would hand
                   every later conversion the argument before its own.  The room
                   for that case comes from the string, in roomFor(). */
                emit( xlat, sizeof xlat, at, "." );
                if ( s.starPrec )
                    emit( xlat, sizeof xlat, at, "*" );
                else
                    emitNumber( xlat, sizeof xlat, at, s.prec );
            }
            /* A bare %s used to be rewritten with a `.1024` precision, which is
               one more thing this formatter did that the caller had not asked
               for.  It is not needed: the string was measured while the arguments
               were walked, so the temporary is the size the string is, and what
               the caller gets is what snprintf promises -- the text truncated to
               its own buffer, not to a limit invented here. */
            emit( xlat, sizeof xlat, at, s.mod );
        }
        if ( at + 1 >= sizeof xlat )
            return -1;
        xlat[ at++ ] = s.conv;
    }

    if ( at + 1 > sizeof xlat )
        return -1;
    xlat[ at ] = '\0';

    /* Nothing has been allocated and nothing written, which is the only reason a
       refusal here is cheap: the caller's buffer still holds what it held, apart
       from the empty string put there at the top. */
    if ( refuse )
        return -1;

    char  stack[ kStackTemp ];
    char *tmp = stack;
    if ( room > (long) sizeof stack ) {
        tmp = (char *) ::malloc( (size_t) room );
        if ( !tmp )
            return -1;
    }

    tmp[0] = '\0';
    const int len = ::vsprintf( tmp, xlat, args );
    if ( len < 0 ) {
        if ( tmp != stack )
            ::free( tmp );
        return -1;
    }

    /* n == 0 reaches here with the text formatted and nothing to write to; `n - 1`
       would be the wrong size to copy in that case, and would not fit in a
       size_t either. */
    if ( n > 0 ) {
        const size_t keep = ( (size_t) len < n - 1 ) ? (size_t) len : n - 1;
        if ( keep )
            ::memcpy( buf, tmp, keep );
        buf[ keep ] = '\0';
    }

    if ( tmp != stack )
        ::free( tmp );
    return len;
}

inline int snprintf( char *buf, size_t n, const char *fmt, ... )
{
    va_list args;
    va_start( args, fmt );
    const int rc = vsnprintf( buf, n, fmt, args );
    va_end( args );
    return rc;
}

/* The names the header is meant to give.  All measured in namespace std: this
   compiler will not publish a name the C header hides behind a macro, which is
   why <cctype> is written the way it is and why none of the macros
   (getc, putc, putchar, eof) appear here. */
using ::size_t;
using ::FILE;
using ::fpos_t;

using ::printf;
using ::fprintf;
using ::sprintf;
using ::vsprintf;
using ::fflush;
using ::fopen;
using ::fclose;
using ::fread;
using ::fwrite;
using ::fgetc;
using ::fputc;
using ::fgets;
using ::fputs;
using ::fseek;
using ::ftell;
using ::rewind;
using ::remove;
using ::rename;
using ::tmpfile;
using ::tmpnam;
using ::setvbuf;
using ::fdopen;
using ::feof;
using ::ferror;

} // namespace std

#endif /* ATUI_BCC_COMPAT_CSTDIO_H */
