/* Does this RTL already declare snprintf and vsnprintf?

   tvision/util.h declares them itself under `#if defined( __BORLANDC__ )`,
   because Borland's RTL has no snprintf, and declares stricmp/strnicmp/strupr/
   itoa/ltoa/ultoa under `#elif !defined( _WIN32 )`, because Unix RTLs have no
   DOS names.  Watcom is a DOS-named RTL, so the question for it is which of the
   two branches it belongs in -- and getting that wrong is not benign in either
   direction: declaring a function the RTL already declares with a different
   calling convention is E619 "linkage specification is different", and failing
   to declare one it lacks is an undeclared-function error.

   Six of the eight are already settled: tools/os2/probe_libs.c calls
   stricmp/strnicmp/strupr/strlwr/itoa/ltoa/ultoa and links, so Watcom has them.
   This file settles snprintf and vsnprintf, and it is a compile-and-call test
   rather than a declaration test on purpose: if the RTL does not declare them,
   the call is an error here and the answer is unambiguous.

   Build (Open Watcom, OS/2):

     wpp386 -bt=os2 -5 -od -w3 -xs -fo=snf.obj probe_snprintf.cpp
     wlink system os2v2 name snf.exe file snf.obj
*/

#include <stdio.h>
#include <stdarg.h>

static int emit( char *buffer, size_t size, const char *format, ... )
{
    va_list args;
    int     written;

    va_start( args, format );
    written = vsnprintf( buffer, size, format, args );
    va_end( args );
    return written;
}

int main( void )
{
    char buffer[ 32 ];
    int  n = snprintf( buffer, sizeof( buffer ), "%s=%d", "answer", 42 );

    printf( "snprintf returned %d, wrote '%s'\n", n, buffer );
    n = emit( buffer, sizeof( buffer ), "%s=%d", "via_va", 7 );
    printf( "vsnprintf returned %d, wrote '%s'\n", n, buffer );
    printf( "SNPRINTF-OK\n" );
    return 0;
}
