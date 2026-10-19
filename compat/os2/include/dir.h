/* dir.h -- the Borland directory header, for the one compiler whose RTL has no
   version of it.

   tvision's classic layer includes <dir.h> for findfirst/findnext/fnmerge and
   struct ffblk (six files do: stddlg.cpp, tdircoll.cpp, tdirlist.cpp,
   tfillist.cpp and friends).  Open Watcom has no dir.h at all -- checked
   against D:\WATCOM\h, where io.h, dos.h, mem.h, bios.h and fcntl.h are all
   present -- so tvision's own emulation in
   tvision/include/tvision/compat/borland/dir.h is what this build has to find.

   It gets there through this one-line forwarder rather than by putting
   tvision's compat/borland directory on the include path, and that is not
   tidiness: an -i directory is searched with Watcom's filename rules, which try
   the requested name with a .h suffix appended, and an appended-.h match in an
   -i directory beats an exact match in Watcom's own headers.  Measured, with a
   directory containing nothing but an iostream.h that defines a marker macro:
   `#include <iostream>` found it and not D:\WATCOM\h\iostream.  So adding
   compat/borland to the path would quietly hijack iostream, fstream, iomanip,
   strstream, mem, iosfwd -- every standard header whose Borland spelling is the
   same name plus .h, which is most of them.

   tvision's compat headers reach their own siblings (dir.h includes "_defs.h")
   with quoted includes, which resolve relative to the including file; angle
   includes do not, which was measured rather than assumed (a header asking for
   <sibling> with a sibling.h next to it did not find it).  So _defs.h and
   _null.h need no forwarder here: nothing outside that directory asks for them
   by angle except in branches of the compat headers that only Borland takes.
*/

#ifndef ATUI_OS2_COMPAT_DIR_H
#define ATUI_OS2_COMPAT_DIR_H

#include <tvision/compat/borland/dir.h>

#endif /* ATUI_OS2_COMPAT_DIR_H */
