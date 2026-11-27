/* cstdlib -- <stdlib.h> in namespace std.

   Borland C++ 5.02 has no <cstdlib>; sixteen of atui's files ask for it and are
   told the file cannot be opened.  The names are published with
   using-declarations, measured one by one: everything the standard header gives
   is a function or a typedef in this CRT and so can be named, and nothing here
   is hidden behind a macro the way <cctype>'s classifications are (see that
   header for what happens when a name is).

   std::atol is on the list and worth mentioning because the summary of this
   compiler written before the measurement claimed it was missing: it is there,
   and the earlier claim was an artefact of a probe that included nothing and
   asked for std::atol.  What is genuinely missing from this CRT is snprintf,
   which is why <cstdio> is not this short.
*/
#ifndef ATUI_BCC_COMPAT_CSTDLIB_H
#define ATUI_BCC_COMPAT_CSTDLIB_H

#include <stddef.h>
#include <stdlib.h>

namespace std {

using ::size_t;
using ::div_t;
using ::ldiv_t;

using ::abort;
using ::abs;
using ::atof;
using ::atoi;
using ::atol;
using ::div;
using ::exit;
using ::free;
using ::getenv;
using ::labs;
using ::ldiv;
using ::malloc;
using ::calloc;
using ::realloc;
using ::rand;
using ::srand;
using ::strtod;
using ::strtol;
using ::strtoul;
using ::system;
using ::bsearch;
using ::qsort;

} // namespace std

#endif /* ATUI_BCC_COMPAT_CSTDLIB_H */
