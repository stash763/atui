/* iostream -- the four stream objects, in the namespace the portable code asks for.

   Borland C++ 5.02 has no <iostream>; one of atui's files asks for it and is told
   the file cannot be opened, and the file is main.cpp, so this is not a header a
   build could do without.  What it wants from the header is one thing, `std::cin`,
   for the `--commands -` path that reads a line at a time (src/core/lines.h takes
   the stream and is the seam that keeps everything else off it).

   The objects are the interesting part, because on this compiler the types and
   the objects are in two different worlds.  The classic iostreams are the only
   streams there are and they live in the global namespace -- see fstream.h in this
   directory for how `std::istream` and friends get published from them, and for
   why the real header can only be reached by its own path through the generated
   atui_bcc_root.h.  `cin`, `cout`, `cerr` and `clog` are objects of those same
   global classes, so publishing them is the same one-line-each trick: a
   using-declaration onto the name the classic header declares, which makes
   `std::cin` the very object `std::istream` names.  Measured by compiling a
   `std::istream &s() { return std::cin; }` and by linking a program that reads
   from it, not by assuming the two headers agree.

   Nothing else is published.  There is no std::endl on this target (the classic
   `endl` is a global function template and taking its address for a different
   char type is not something this library does), no std::hex manipulators in
   namespace std, and atui uses none of them -- which is the test this layer
   applies to every header: publish what the program asks for, measured, and leave
   the rest alone rather than inventing an interface nobody has checked.
*/
#ifndef ATUI_BCC_COMPAT_IOSTREAM_H
#define ATUI_BCC_COMPAT_IOSTREAM_H

/* Written by tools/bcc/bcc-run.sh and build.cmd: the installation's own
   iostream.h, by path.  `<iostream.h>` from here would open this file. */
#include <atui_bcc_root.h>

#include ATUI_BCC_INCLUDE_IOSTREAM

/* The types, because <iostream> is allowed to be the only stream header a file
   includes and src/core/lines.h names std::istream in its signature. */
#include <fstream>

namespace std {

using ::cin;
using ::cout;
using ::cerr;
using ::clog;

} // namespace std

#endif /* ATUI_BCC_COMPAT_IOSTREAM_H */
