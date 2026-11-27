/* chrono -- <chrono> for the era Windows build.

   No <chrono> in Borland C++ 5.02, and atui needs elapsed time in milliseconds
   in three places: the deadline on a tool's subprocess, the timeout on the
   approval handshake, and how long the permission judge took, which goes into
   the log because a judge that takes nine seconds is a fact about the program
   worth recording.

   Only what atui uses is here:

      std::chrono::steady_clock::now()
      std::chrono::steady_clock::time_point     + duration, - time_point
      std::chrono::milliseconds, std::chrono::seconds
      std::chrono::duration_cast< milliseconds >( x ).count()

   `duration` is a class rather than the standard's template, and milliseconds
   and seconds derive from it by conversion rather than being names for the same
   type -- the same shape as compat/os2/include/chrono, and for the same reason:
   if seconds were another name for milliseconds, `now() + seconds( 30 )` would
   compile, add thirty milliseconds, and show up as a command killed almost
   immediately with nothing in the code to point at.

   The clock is QueryPerformanceCounter, a 64-bit counter whose rate comes from
   QueryPerformanceFrequency, and it is monotonic by construction.  Where that is
   not available -- the API is documented as unsupported on machines with no
   high-resolution counter -- the fallback is GetTickCount at its usual ten or
   sixteen milliseconds, extended to 64 bits by counting the wraps: GetTickCount
   goes back to zero after 49.7 days, and a deadline that a wrap moves four and a
   half weeks into the past is a hung tool call with no way to see why.  The
   granularity of the fallback is worth knowing about when reading a timeout that
   fired a couple of milliseconds late on a machine that old.

   64-bit arithmetic is `__int64` rather than `long long`, and 64-bit literals
   end in I64 rather than LL: this compiler rejects both portable spellings
   (`long long` is "Too many types in declaration", `1000LL` is a syntax error)
   and accepts its own.  The multiply is kept off the 64-bit counter -- ticks are
   divided first and only the remainder multiplied -- because a counter running
   at a few gigahertz overflows in a month otherwise, and a month is not longer
   than a program on this kind of machine might run.
*/
#ifndef ATUI_BCC_COMPAT_CHRONO_H
#define ATUI_BCC_COMPAT_CHRONO_H

#include <windows.h>
#include <stdlib.h>

namespace std {
namespace chrono {

inline __int64 elapsedMs();

/* Every duration is a count of milliseconds whatever it was written as.
   count() is the standard spelling; ms() is the same number and exists because
   the conversions below would otherwise reach into each other's privates. */
class duration {
public:
    duration()
        : ms_( 0 ) {}
    duration( __int64 ms )
        : ms_( ms ) {}

    __int64 count() const { return ms_; }
    __int64 ms()    const { return ms_; }

private:
    __int64 ms_;
};

class milliseconds : public duration {
public:
    explicit milliseconds( __int64 ms )
        : duration( ms ) {}
};

class seconds : public duration {
public:
    explicit seconds( __int64 s )
        : duration( s * 1000I64 ) {}

    /* count() of a seconds value counts seconds, as it does in the standard;
       the milliseconds are the representation, not the meaning. */
    __int64 count() const { return ms() / 1000I64; }
};

/* A change of representation rather than of value, and the only conversion atui
   asks for is "that, in milliseconds".

   This is a class and the call is a construction, not a function call.  It is
   written that way because the compiler, not because the standard is loose
   about it: a function template whose parameter is named only in its return
   type cannot be called at all here --

      template< class To > To duration_cast( const duration & );
      duration_cast< milliseconds >( d )     // "Type mismatch in redeclaration"

   -- measured with the arguments in the parameters (so not the qualified-name
   problem <chrono.h> would otherwise have had), with the ARM's `class To`
   spelling, with an explicit instantiation, and with a second defaulted
   template parameter, all of which are refused.  What the compiler does accept
   is the name of a class template followed by `< To >` and a parenthesised
   argument, which is an explicit construction and means the same thing at this
   call: the expression's type is a temporary holding the converted count, and
   `count()` on it is the number the standard's duration_cast would have
   answered with.  Two consequences worth knowing: `&duration_cast` is not a
   thing to be taken, and the temporary cannot be assigned to a `milliseconds`
   -- atui only ever writes `duration_cast< X >( d ).count()`, which is what
   this is for, and anything else will not compile rather than mean something
   quietly different. */
template< class To >
class duration_cast {
public:
    explicit duration_cast( const duration &d )
        : v_( To( d.ms() ).count() ) {}

    /* The count in To's units, which is what makes `To( ms ).count()` the right
       body: milliseconds counts milliseconds, seconds counts seconds. */
    __int64 count() const { return v_; }

private:
    __int64 v_;
};

class time_point {
public:
    time_point()
        : ms_( 0 ) {}
    explicit time_point( const duration &d )
        : ms_( d.ms() ) {}

    duration time_since_epoch() const { return duration( ms_ ); }

    time_point operator+( const duration &d ) const { return time_point( duration( ms_ + d.ms() ) ); }
    time_point operator-( const duration &d ) const { return time_point( duration( ms_ - d.ms() ) ); }

    duration operator-( const time_point &o ) const { return duration( ms_ - o.ms_ ); }

    bool operator<( const time_point &o )  const { return ms_ <  o.ms_; }
    bool operator>( const time_point &o )  const { return ms_ >  o.ms_; }
    bool operator<=( const time_point &o ) const { return ms_ <= o.ms_; }
    bool operator>=( const time_point &o ) const { return ms_ >= o.ms_; }

private:
    __int64 ms_;
};

struct steady_clock {
    typedef __int64            rep;
    typedef chrono::duration   duration;
    typedef chrono::time_point time_point;

    static time_point now()
    {
        return time_point( duration( elapsedMs() ) );
    }
};

/* The counter, in milliseconds. */
inline __int64 elapsedMs()
{
    static __int64 freq    = 0;         /* 0 until asked, -1 if there is none */
    static __int64 wrapped = 0;         /* ticks seen past GetTickCount's wrap */
    static DWORD     last    = 0;

    if ( 0 == freq ) {
        LARGE_INTEGER f;
        if ( ::QueryPerformanceFrequency( &f ) && f.QuadPart > 0 )
            freq = (__int64) f.QuadPart;
        else
            freq = -1;
    }

    if ( freq > 0 ) {
        LARGE_INTEGER c;
        if ( !::QueryPerformanceCounter( &c ) )
            ::abort();
        /* Divide before multiplying: at a few gigahertz the product would
           overflow the counter's own width inside a month. */
        const __int64 ticks  = (__int64) c.QuadPart;
        const __int64 whole  = ticks / freq;
        const __int64 across = ticks % freq;
        return whole * 1000I64 + ( across * 1000I64 ) / freq;
    }

    const DWORD t = ::GetTickCount();
    if ( t < last )
        wrapped += 0x100000000I64;       /* the counter went round: keep counting */
    last = t;

    return wrapped + (__int64) t;
}

} // namespace chrono
} // namespace std

#endif /* ATUI_BCC_COMPAT_CHRONO_H */
