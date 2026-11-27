/* cstddef -- <cstddef> for a compiler whose C++ headers stop at <cstring>.

   Borland C++ 5.02 has no <cstddef>: `#include <cstddef>` is answered with
   "Unable to open include file 'cstddef.h'", and nine of atui's files ask for
   it.  It owns none of the long <cxx> names either -- <cstdio>, <cstdlib>,
   <cctype>, <ctime>, <cerrno>, <csignal>, <cstdarg> all fail the same way --
   which is why this directory looks the way it does.

   Note what is *not* here.  <cstring> is a name this installation does have,
   and its header (measured: cstring.h, "C/C++ Run Time Library - Version 8.0")
   is a stub that declares nothing in namespace std, so cstring.h in this
   directory replaces it rather than shadowing something useful.  The headers
   that really are Borland's -- <memory>, <utility>, <new>, <typeinfo>, <limits>
   and the Rogue Wave containers -- are left alone: nothing here may own a name
   the compiler needs to find its own, because the compiler finds this directory
   first and cannot be told not to.

   There is no std::nullptr_t, because there is no nullptr: __cplusplus is 1
   here, so the null pointer constant is still 0.
*/
#ifndef ATUI_BCC_COMPAT_CSTDDEF_H
#define ATUI_BCC_COMPAT_CSTDDEF_H

#include <stddef.h>

namespace std {

using ::size_t;
using ::ptrdiff_t;

} // namespace std

#endif /* ATUI_BCC_COMPAT_CSTDDEF_H */
