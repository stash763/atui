/* cstring -- <string.h> in namespace std, over the top of the stub.

   This is the one name in this directory that Borland also owns, and it is the
   exception that proves the rule in README.md: cstring.h in
   C:\BC5\INCLUDE is 1997's idea of a C++ header and declares nothing (measured:
   `std::strlen` through it is "Qualifier 'std' is not a class or namespace
   name").  Replacing a header that provides nothing is not shadowing something
   useful, and the include order that makes it work -- this directory first, the
   installation second, and no way to tell the compiler otherwise -- is the same
   one that makes <fstream> need a generated path instead.

   What it forwards to is <string.h>, which is not shadowed and is the real
   thing.  The names are the C library's own, measured publishable with
   using-declarations: isspace and friends are on this list, and they are the
   reason <cctype> cannot be written this way -- in ctype.h the classifications
   are also macros, and a using-declaration cannot name a macro.
*/
#ifndef ATUI_BCC_COMPAT_CSTRING_H
#define ATUI_BCC_COMPAT_CSTRING_H

#include <stddef.h>
#include <string.h>

namespace std {

using ::size_t;

using ::memchr;
using ::memcmp;
using ::memcpy;
using ::memmove;
using ::memset;
using ::strcat;
using ::strchr;
using ::strcmp;
using ::strcpy;
using ::strcspn;
using ::strlen;
using ::strncat;
using ::strncmp;
using ::strncpy;
using ::strpbrk;
using ::strrchr;
using ::strspn;
using ::strstr;
using ::strtok;
using ::strerror;

} // namespace std

#endif /* ATUI_BCC_COMPAT_CSTRING_H */
