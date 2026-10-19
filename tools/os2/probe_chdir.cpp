/* Which header declares chdir on this RTL?

   tvision's compat/borland/dir.h declares the Borland directory helpers, and for
   anything that is not _WIN32 it gets them by including <unistd.h>.  That is
   right on Linux and FreeBSD, where <unistd.h> declares chdir.  On Open Watcom
   for OS/2 the classic layer fails with "symbol 'chdir' has not been declared"
   (tchdrdlg.cpp:198), so something has to give, and it should be decided by
   asking the compiler rather than by reading the Watcom docs and hoping.

   The file prints which of the candidate headers provided the declaration.  A
   candidate that does not declare chdir is not an error here: the call is
   written after all the includes, so the test is whether this compiles at all
   with a given header, one at a time, via -DTRY_<header>.

   Build and run (Open Watcom, OS/2):

     wpp386 -bt=os2 -5 -od -w3 -xs -dTRY_UNISTD  -fo=cd1.obj probe_chdir.cpp
     wpp386 -bt=os2 -5 -od -w3 -xs -dTRY_DIRECT  -fo=cd2.obj probe_chdir.cpp
     wpp386 -bt=os2 -5 -od -w3 -xs -dTRY_IO      -fo=cd3.obj probe_chdir.cpp

   Whatever compiles declares it.  -DTRY_ALL compiles nothing extra and is the
   control: if that one succeeds then chdir came from something Watcom includes
   implicitly, and the failure is elsewhere.
*/

#include <stdio.h>

#if defined( TRY_UNISTD )
#include <unistd.h>
#elif defined( TRY_DIRECT )
#include <direct.h>
#elif defined( TRY_IO )
#include <io.h>
#endif

int main( void )
{
    printf( "chdir(\".\") -> %d errno-shaped result\n", chdir( "." ) );
    printf( "CHDIR-OK\n" );
    return 0;
}
