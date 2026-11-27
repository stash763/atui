/* fstream -- the classic streams, in the namespace the portable code asks for.

   Borland C++ 5.02 has the Rogue Wave containers in namespace std -- string,
   vector, map, the lot -- and its streams nowhere near it: they are the classic
   pre-standard iostreams in the global namespace, and there is no std::ostream
   to be had from any header this installation owns (measured across all of them:
   the Rogue Wave set is algorith, bitset, complex, deque, function, iterator,
   limits, list, map, memory, numeric, queue, random, set, stack, string, tree,
   utility, vector -- containers only).  So `std::ifstream` in four of atui's
   files needs the names publishing, and that is what this file is for.

   The interesting part is how it reaches the classes in the first place.  This
   directory is searched before the installation and BC5 has no rule for falling
   back, so `#include <fstream.h>` from here would open this file: the include
   guard would stop the recursion and nothing would be declared, which is the
   silent failure mode README.md is largely about.  The real header can only be
   reached by its own path, and BC5 expands a macro in an #include directive --
   so the path comes from a header the build driver writes, because that is the
   one form of quoting that survives the response file (measured: -D with quotes
   works on the command line and not through @args.rsp, which is how an era build
   passes flags; a generated header sidesteps the question).

   What is published is the set the portable code touches, measured: the open
   modes reach through `std::ios::in` as enumerators of the classic ios, which
   works, but the *type* `std::ios::openmode` does not exist here at all -- the
   classic ios has no such member typedef -- so nothing that names the type will
   compile on this target and atui does not: it passes the flags inline.  There is
   no std::endl either, for the same reason (it is a global function here), and
   `is_open()` is not what the classic stream calls it; atui uses neither.
*/
#ifndef ATUI_BCC_COMPAT_FSTREAM_H
#define ATUI_BCC_COMPAT_FSTREAM_H

/* Written by tools/bcc/build.cmd.  It defines ATUI_BCC_INCLUDE_FSTREAM to the
   quoted path of the installation's own fstream.h. */
#include <atui_bcc_root.h>

#include ATUI_BCC_INCLUDE_FSTREAM

namespace std {

using ::ios;
using ::istream;
using ::ostream;
using ::ifstream;
using ::ofstream;
using ::fstream;
using ::streampos;

} // namespace std

#endif /* ATUI_BCC_COMPAT_FSTREAM_H */
