// Reading the clock into a struct tm on a library without the re-entrant forms.
//
// gmtime_r() and localtime_r() are POSIX.  Watcom's C library has only gmtime()
// and localtime(), which return a pointer to one static struct (measured: the
// compiler has not heard of the _r spellings).  A caller should not have to know
// which kind of library it is on, so the conversion is here and said once:
//
//   where the re-entrant form exists it is used; where it does not, the result
//   is copied out of the library's static struct before returning, so no caller
//   holds that pointer and no caller has to lock anything to read a broken-down
//   time.  What is not available on those compilers is two threads calling this
//   at the same instant -- Watcom's build has one thread (see
//   compat/os2/include/README.md), and the places that use it (an undo stamp, a
//   permission log line, the get_time tool) already run one at a time.
//
// Returns false only when the library refuses to break the time down at all,
// which is what a caller reports; it never leaves `out` partly filled.

#ifndef ATUI_CORE_CLOCK_H
#define ATUI_CORE_CLOCK_H

#include <ctime>

namespace atui {
namespace core {

// `when` as calendar time in UTC.
bool utcBreakdown( std::time_t when, std::tm &out );

// `when` as calendar time in the machine's local zone.
bool localBreakdown( std::time_t when, std::tm &out );

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_CLOCK_H
