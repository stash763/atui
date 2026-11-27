/* cstdarg -- <stdarg.h> in namespace std, for the one type atui names.

   src/net/dbg.h takes a `...` and passes it on, so it needs std::va_list and
   nothing else; the macros (va_start, va_arg, va_end) stay macros, as the
   standard allows and this CRT insists.  va_list here is `void _FAR *`
   (stdarg.h line 28), which is why the assignment in <cstdio> is a copy of the
   cursor rather than an alias to the caller's -- and why that header can walk an
   argument list to bound an output and then print from the beginning of it.
*/
#ifndef ATUI_BCC_COMPAT_CSTDARG_H
#define ATUI_BCC_COMPAT_CSTDARG_H

#include <stdarg.h>

namespace std {

using ::va_list;

} // namespace std

#endif /* ATUI_BCC_COMPAT_CSTDARG_H */
