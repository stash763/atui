/* dirent.h -- <dirent.h> for the one compiler that has no version of it.

   One file of atui includes <dirent.h>, to list a directory: the undo journal
   walking its install directory at startup for the .PRE files a request that died
   left behind.  POSIX spells that header dirent.h; Open Watcom spells it
   direct.h, and has everything in it -- `struct dirent` with `d_name`, `typedef
   struct dirent DIR`, opendir, readdir, closedir, rewinddir, alongside the
   mkdir/rmdir/getcwd/chdir that the same files already call.  So this is a
   forwarder, like dir.h and alloc.h next to it.

   It was not written this way first.  The first version was a re-implementation
   over _findfirst/_findnext/_findclose from <io.h>, on the evidence that direct.h
   has no findfirst in it -- and the compile said otherwise in three lines, because
   including both defines `dirent` twice:

       D:\WATCOM\H\direct.h(87): Error! E126: class 'dirent' has already been defined
       D:\WATCOM\H\direct.h(98): Error! E125: invalid redefinition of the typedef name 'DIR'
       D:\WATCOM\H\direct.h(138): Error! E385: attempt to overload function 'opendir'
                                  with a different return type

   Which is the lesson worth keeping: Watcom's RTL has more than one way to do most
   things, and searching for the POSIX *name* is not the same as searching for the
   POSIX *thing*.  `grep findfirst direct.h` said "no directory walking here" and
   was wrong; `grep opendir direct.h` says the truth.  Two hundred lines of shim
   replaced by one.

   Nothing else in dirent.h is missing, but two properties of this RTL version are
   worth knowing from here: `NAME_MAX` is 255 under `__OS2__` (12 for a plain DOS
   build, where a long name is truncated rather than wrapped), and there is no
   `d_type` -- the attribute is `d_attr`, an `_A_` constant -- so a portable caller
   cannot tell a directory from a file without stat.  The journal only needs the
   names, and filters on extension.
*/
#ifndef ATUI_OS2_COMPAT_DIRENT_H
#define ATUI_OS2_COMPAT_DIRENT_H

#include <direct.h>

#endif /* ATUI_OS2_COMPAT_DIRENT_H */
