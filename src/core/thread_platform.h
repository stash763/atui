// The seam between a thread and the platform that provides it.  Internal to
// core/thread*.cpp -- nothing else includes this, which is the point: there are
// exactly three implementations and one caller, and a header that lets a fourth
// appear without being noticed is a header that lies about how many platforms work.
//
// Each implementation lives in its own file, empty unless its platform macro is set,
// because the three disagree about everything except this shape:
//
//   POSIX    pthread_create / pthread_join          (Linux, FreeBSD, macOS host)
//   OS/2     _beginthread + an event semaphore      (Watcom, -bm)
//   Windows  _beginthreadex + the thread handle     (MSVC, MinGW, Watcom NT target)
//
// and set ATUI_HAVE_POSIX_THREADS / ATUI_HAVE_OS2_THREADS / ATUI_HAVE_WIN_THREADS
// accordingly.  A build that sets one of those and has no matching implementation
// fails at the link, which is the loudest available answer: `threadsAvailable()`
// would otherwise promise threads and `start()` would call nothing.
//
// Why `_beginthread`/`_beginthreadex` rather than the kernel's own call on the two
// platforms that have one: the C runtime has per-thread state -- the heap's lock and
// bookkeeping, `errno`, the signal state, stdio's buffers -- and its start-up wrapper
// is what sets that up.  `DosCreateThread` measured working on the OS/2 target and so
// did `_beginthread`, but only the second one is known to leave `malloc` correct, and
// a program that allocates a `std::string` in a thread whose heap was never
// initialised per-thread is a corruption bug with a delay.  Joining is the other half,
// and on OS/2 there is nothing to join: `_beginthread` reports no handle, hence the
// event semaphore it posts on the way out (thread_os2.cpp).
#ifndef ATUI_CORE_THREAD_PLATFORM_H
#define ATUI_CORE_THREAD_PLATFORM_H

#include "thread.h"

#include <string>

namespace atui {
namespace core {

// Creates the thread, which will run `Thread::runFor( self )`, and stores whatever
// `threadPlatformJoin` needs in `handle` -- two pointer-sized words, owned by the
// Thread.  Returns false with a reason when the platform refuses; the caller runs the
// task on its own stack, so an honest failure is always better than a half-started
// thread, and "out of memory" is a normal answer on the targets that matter.
bool threadPlatformStart( Thread &self, void **handle, std::string *reason );

// Waits for the thread to return from its task and releases everything `handle`
// refers to.  Called at most once per thread, by the thread that started it.
void threadPlatformJoin( void **handle );

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_THREAD_PLATFORM_H
