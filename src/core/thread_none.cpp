// The build that has no threads: DOS, and anything else that has not been given a
// thread library.
//
// Both hooks exist so that `core/thread.cpp` links on every platform, and both refuse
// rather than pretending.  That is the point of the file: on these targets the honest
// answer to "run this away from the screen" is that there is nowhere else to run it,
// and the caller keeps the work on its own stack.  What a caller does then is the
// interesting question and it is not a threading question at all: it steps several
// state machines from a timer, which is what PORTING.md calls the cooperative
// scheduler, and which needs no thread and no emulation of one.
//
// `Thread::start()` never reaches these functions when `threadsAvailable()` is false,
// so they are also a second line of defence in case that check is ever removed: a
// caller that asks for a thread on a platform without one gets a refusal and a reason,
// never a thread that does not exist.
#include "thread_platform.h"

#ifndef ATUI_HAVE_THREADS

#include <string>

namespace atui {
namespace core {

bool threadPlatformStart( Thread & /*self*/, void ** /*handle*/, std::string *reason )
{
    if( reason )
        *reason = "this build has no threads";
    return false;
}

void threadPlatformJoin( void ** /*handle*/ )
{
}

} // namespace atui::core
} // namespace atui

#endif // !ATUI_HAVE_THREADS
