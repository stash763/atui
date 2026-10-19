/* Which Watcom header declares std::streampos and std::streamoff?

   tvision's compat/borland/iosfwd.h does:

       #include <iosfwd>
       using std::ostream;
       using std::streambuf;
       using std::streampos;
       using std::streamoff;

   and on Open Watcom the first two resolve while the last two do not: the
   compiler says "'std::streampos' has not been declared as a member".  So
   Watcom's <iosfwd> is not the whole forward declaration set, and the two
   typedefs live somewhere else -- the library does define them (`typedef long
   streampos` is in there), just not where tvision looks.  Every file that
   includes tv.h hits this, so it is one of the first things standing between the
   fork and a compile.

   Rather than guess which header to add, ask: compile the same four using-
   declarations against one candidate each and see which combination compiles.
   The candidates are tried in ascending weight, because the header that provides
   a typedef without dragging in the whole iostream machinery is the one worth
   having in every translation unit.

   Build (Open Watcom, OS/2), once per candidate:

     wpp386 -bt=os2 -5 -od -w3 -xs -DPROBE_HDR=2 -fo=h2.obj probe_hdr.cpp
*/

#include <iosfwd>

#if defined( PROBE_HDR ) && ( PROBE_HDR == 2 )
#include <streambuf>
#elif defined( PROBE_HDR ) && ( PROBE_HDR == 3 )
#include <ios>
#elif defined( PROBE_HDR ) && ( PROBE_HDR == 4 )
#include <iostream>
#elif defined( PROBE_HDR ) && ( PROBE_HDR == 5 )
#include <fstream>
#endif

using std::ostream;
using std::streambuf;
using std::streampos;
using std::streamoff;

int main( void )
{
    streampos  p = 0;
    streamoff  o = 0;
    (void) p;
    (void) o;
    return 0;
}
