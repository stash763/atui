/* What the compiler tells you about itself, dumped rather than remembered.

   tv.h decides the whole platform shape from a handful of macros.  If __FLAT__ is
   not defined it takes the 16-bit branch and defines `_NEAR` as `near`, which in a
   32-bit flat compile is not a keyword but a syntax error; if _TV_UNIX gets defined
   for a compiler that is not Unix, the build inherits termios, signals and
   /dev/tty.  Both decisions turn on whether Watcom supplies __FLAT__ itself, and
   whether it needs __cdecl/__stdcall emptied out the way the non-Microsoft
   compilers handled by that file do.

   So print what is actually defined.  Guessing here means editing a header every
   file in tvision includes, on the strength of something remembered about a
   different compiler.

   Flags and values are reported by separate helpers on purpose: a flag macro is
   usually defined to nothing, and `(long) __FLAT__` is not an expression.

   Build (Open Watcom, OS/2):

     wpp386 -bt=os2 -5 -od -w3 -xs -fo=md.obj macro_dump.cpp
     wlink system os2v2 name md.exe file md.obj
*/

#include <stdio.h>

static void flag( const char *name, int isSet )
{
    printf( "  %-20s %s\n", name, isSet ? "defined" : "(not defined)" );
}

static void value( const char *name, long v )
{
    printf( "  %-20s = %ld\n", name, v );
}

int main( void )
{
    printf( "== the macros tv.h turns the platform shape on ==\n" );

#ifdef __FLAT__
    flag( "__FLAT__", 1 );
#else
    flag( "__FLAT__", 0 );
#endif

#ifdef __BORLANDC__
    flag( "__BORLANDC__", 1 );
#else
    flag( "__BORLANDC__", 0 );
#endif

    printf( "== identity, as the port will test it ==\n" );
#ifdef __WATCOMC__
    value( "__WATCOMC__", __WATCOMC__ );
#else
    flag( "__WATCOMC__", 0 );
#endif
#ifdef __cplusplus
    value( "__cplusplus", __cplusplus );
#else
    flag( "__cplusplus", 0 );
#endif
#ifdef __STDC__
    value( "__STDC__", __STDC__ );
#else
    flag( "__STDC__", 0 );
#endif
#ifdef __STDC_VERSION__
    value( "__STDC_VERSION__", __STDC_VERSION__ );
#else
    flag( "__STDC_VERSION__", 0 );
#endif

    /* Target-family macros: which of these Watcom defines decides whether an
       OS/2 build can be told apart from a Windows one by the preprocessor alone,
       or whether the port has to pass -D__OS2__ itself. */
#ifdef __OS2__
    value( "__OS2__", __OS2__ );
#else
    flag( "__OS2__", 0 );
#endif
#ifdef __NT__
    value( "__NT__", __NT__ );
#else
    flag( "__NT__", 0 );
#endif
#ifdef __386__
    flag( "__386__", 1 );
#else
    flag( "__386__", 0 );
#endif
#ifdef __X86__
    flag( "__X86__", 1 );
#else
    flag( "__X86__", 0 );
#endif
#ifdef __MT__
    flag( "__MT__", 1 );
#else
    flag( "__MT__", 0 );
#endif

    printf( "== sizes, so a wrong macro cannot quietly agree with a wrong type ==\n" );
    printf( "  int=%d long=%d pointer=%d long_long=%d wchar_t=%d size_t=%d\n",
            (int) sizeof( int ), (int) sizeof( long ), (int) sizeof( void * ),
            (int) sizeof( long long ), (int) sizeof( wchar_t ), (int) sizeof( size_t ) );
    printf( "  control (a number here means the program really ran): __LINE__=%d\n",
            __LINE__ );
    return 0;
}
