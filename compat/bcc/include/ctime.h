/* ctime -- <time.h> in namespace std.

   No <ctime> in this installation; six of atui's files ask for it.  Everything
   the standard header is made of is declared by this CRT's <time.h> and each
   name was measured publishing with a using-declaration, including the two
   struct tags (this compiler will name `struct tm` with `using ::tm;`).

   What is *not* here, because this CRT does not have it, is the re-entrant
   pair: no gmtime_r, no localtime_r, and no localtime_s either -- all three
   measured absent.  That is the fact src/core/clock.cpp is written around: the
   branch it takes when neither __unix__ nor __APPLE__ is defined calls
   std::localtime and copies out of the result, which is the only thing there is
   on a target like this.  It is thread-unsafe in the way every CRT of this era
   is thread-unsafe, so a second thread breaking down a timestamp is a data race
   on the one static struct; atui has one thread doing time formatting, and the
   place to look if that changes is clock.cpp rather than here.
*/
#ifndef ATUI_BCC_COMPAT_CTIME_H
#define ATUI_BCC_COMPAT_CTIME_H

#include <stddef.h>
#include <time.h>

namespace std {

using ::size_t;
using ::clock_t;
using ::time_t;
using ::tm;
using ::clock;
using ::difftime;
using ::time;
using ::asctime;
using ::ctime;
using ::gmtime;
using ::localtime;
using ::mktime;
using ::strftime;

} // namespace std

#endif /* ATUI_BCC_COMPAT_CTIME_H */
