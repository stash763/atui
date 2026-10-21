// The part of a thread that is the same on every platform: a task, run once, and a
// report that it is over.
//
// The platform differences are in thread_posix.cpp, thread_os2.cpp and
// thread_win.cpp, each empty on the platforms it is not for -- the arrangement
// net/sock_*.cpp uses for the same reason.  What stays here is the ordering, because
// the ordering is where the bugs are: a thread that reports itself finished before the
// task has returned lets its caller destroy the task while it is still being run.
#include "thread.h"
#include "thread_platform.h"

namespace atui {
namespace core {

// static
void Thread::runFor( void *self )
{
    Thread &t = *static_cast< Thread * >( self );
    if( t.task_ != 0 )
        t.task_->run();
    // Cleared before the platform reports completion, so a task that outlives the
    // thread by design cannot be run twice by a second join/start sequence, and a
    // task that the caller frees after join() is never touched after this point.
    t.task_ = 0;
}

bool threadsAvailable()
{
#ifdef ATUI_HAVE_THREADS
    return true;
#else
    // DOS, and any build that has not chosen a thread library.  Not an error and not
    // a failure to configure: the caller runs its work on its own stack, and on DOS
    // that is what a timer-driven scheduler is for (PORTING.md).
    return false;
#endif
}

Thread::Thread()
    : task_( 0 )
    , running_( false )
{
    platform_[ 0 ] = 0;
    platform_[ 1 ] = 0;
}

Thread::~Thread()
{
    // Not join(): a destructor that blocks is worse than the bug it prevents, and
    // every caller that starts a thread has to join it for the task's lifetime to
    // make sense in the first place.  A thread still running here is a programming
    // error, and in a debug build it is worth stopping for.
    running_ = false;
}

bool Thread::start( Task &task, std::string *reason )
{
    if( running_ )
    {
        if( reason )
            *reason = "this thread is still running";
        return false;
    }
    if( !threadsAvailable() )
    {
        // Wording matters: this is the normal answer on one of the five targets, and
        // the caller's job is to run the work itself, not to show a failure.
        if( reason )
            *reason = "this build has no threads";
        return false;
    }

    task_ = &task;
    if( !threadPlatformStart( *this, platform_, reason ) )
    {
        task_ = 0;
        return false;
    }
    running_ = true;
    return true;
}

void Thread::join()
{
    if( !running_ )
        return;
    threadPlatformJoin( platform_ );
    running_ = false;
}

} // namespace atui::core
} // namespace atui
