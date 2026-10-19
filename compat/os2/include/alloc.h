/* alloc.h -- the Borland allocation header, for the one compiler whose RTL has
   no version of it.

   Four files of the classic layer include <alloc.h> (tcollect.cpp, new.cpp,
   tfiledtr.cpp and compat/borland's own alloc.h).  Open Watcom has no alloc.h;
   it has mem.h, which is a different Borland header and is not a substitute for
   the far-heap declarations tvision asks for.  So, as with dir.h, the forwarder
   here points at tvision's emulation and the compat/borland directory stays off
   the include path -- see the note in dir.h about Watcom's filename rules
   turning <iostream> into iostream.h and hijacking the standard library.
*/

#ifndef ATUI_OS2_COMPAT_ALLOC_H
#define ATUI_OS2_COMPAT_ALLOC_H

#include <tvision/compat/borland/alloc.h>

#endif /* ATUI_OS2_COMPAT_ALLOC_H */
