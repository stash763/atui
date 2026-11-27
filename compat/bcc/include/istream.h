/* istream -- `std::istream` for the classic streams.

   There is no <istream> in this installation and no std::istream in any header
   it owns, so the name comes from fstream.h in this directory -- see that file
   for why the publishing lives there rather than here, which is that the classic
   classes are all declared by the same header and one of them has to reach it by
   absolute path.

   src/core/lines.h is what this exists for: readLine() takes a stream to pull
   characters from, and the era build calls it with a std::ifstream.
*/
#ifndef ATUI_BCC_COMPAT_ISTREAM_H
#define ATUI_BCC_COMPAT_ISTREAM_H

#include <fstream.h>

#endif /* ATUI_BCC_COMPAT_ISTREAM_H */
