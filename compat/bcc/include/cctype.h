/* cctype -- <ctype.h> in namespace std, after undefining half of it.

   The classification functions are the one place in this CRT where a
   using-declaration is not enough, and the reason is worth writing down because
   it is invisible from the error message.  ctype.h declares `int _CType
   isdigit( int )` and then, twenty lines later, defines `isdigit( c )` as a
   lookup in the _ctype table.  By the time a header asks for `using ::isdigit;`
   the name is a macro, and a using-declaration cannot name a macro: the
   diagnostic is not "isdigit is a macro" but the cryptic cousin of it, and the
   same is true of all eleven is* names and of toupper and tolower, which are
   object-like macros for _ltoupper and _ltolower.

   So the macros come off first.  That costs something: every user of <cctype>
   after this point calls the function rather than expanding the table lookup,
   which is slower by a memory read or two and means exactly the same thing --
   the functions and the macros are documented to agree, and Borland's own
   <ctype.h> prints both from the same declarations.  The alternative, keeping
   the macros and writing inline wrappers that expand them, would publish std::
   names that behave differently from ::names depending on which header a
   translation unit happened to read last, which is a worse thing to leave behind.

   Measured: with the names undefined, `using ::isdigit;` compiles and the call
   works; without it, the same line fails.
*/
#ifndef ATUI_BCC_COMPAT_CTYPE_H
#define ATUI_BCC_COMPAT_CTYPE_H

#include <ctype.h>

#undef isalnum
#undef isalpha
#undef iscntrl
#undef isdigit
#undef isgraph
#undef islower
#undef isprint
#undef ispunct
#undef isspace
#undef isupper
#undef isxdigit
#undef tolower
#undef toupper

namespace std {

using ::isalnum;
using ::isalpha;
using ::iscntrl;
using ::isdigit;
using ::isgraph;
using ::islower;
using ::isprint;
using ::ispunct;
using ::isspace;
using ::isupper;
using ::isxdigit;
using ::tolower;
using ::toupper;

} // namespace std

#endif /* ATUI_BCC_COMPAT_CTYPE_H */
