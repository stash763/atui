/* Do the DOS-named string and conversion helpers actually exist in this C
   library, at link time?

   tvision declares and defines its own `stricmp`, `strnicmp`, `strupr`, `itoa`,
   `ltoa` and `ultoa` for every non-Microsoft platform (source/platform/strings.cpp
   implements them over strcasecmp and sprintf).  On Watcom those names are already
   in <string.h> and <stdlib.h>, declared `watcall`, so tvision's cdecl
   redeclarations collide -- E619 "linkage specification is different" and E670
   "function modifier conflicts", reported from inside Watcom's own <cstring> and
   pointing at tvision's util.h.

   The fix is to let Watcom's own versions stand.  But "the header declares it" and
   "the library defines it" are different claims, and this build produces a library
   rather than a program, so a missing one would not show up until atui is linked.
   Hence a program that calls all six and has to link: that is the question, and
   nothing else.

   Build (Open Watcom, OS/2):

     wcc386 -bt=os2 -5 -od -w3 -fo=pl.obj probe_libs.c
     wlink system os2v2 name pl.exe file pl.obj
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main( void )
{
    char buf[ 24 ];

    printf( "stricmp=%d strnicmp=%d\n",
            stricmp( "abc", "ABC" ), strnicmp( "abc", "abd", 2 ) );

    strcpy( buf, "abc" );
    printf( "strupr=%s\n", strupr( buf ) );

    strcpy( buf, "abc" );
    printf( "strlwr=%s\n", strlwr( buf ) );

    printf( "itoa=%s\n", itoa( -42, buf, 10 ) );
    printf( "ltoa=%s\n", ltoa( 123456L, buf, 10 ) );
    printf( "ultoa=%s\n", ultoa( 65535UL, buf, 16 ) );
    printf( "LIBS-OK\n" );
    return 0;
}
