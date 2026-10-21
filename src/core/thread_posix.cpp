// POSIX threads: Linux, FreeBSD, and the host every test in this repository runs on.
#include "thread_platform.h"

#if defined( ATUI_HAVE_THREADS ) && !defined( _WIN32 ) && !defined( __OS2__ )

#include <pthread.h>
#include <cerrno>
#include <cstring>

namespace atui {
namespace core {

namespace {

// The C entry point.  pthread wants `void *(*)(void *)` and atui's trampoline returns
// nothing, which is the usual small lie: nothing looks at the return value, and
// anything the task wanted to report is in the task.
void *entry( void *self )
{
    Thread::runFor( self );
    return 0;
}

} // anonymous namespace

bool threadPlatformStart( Thread &self, void **handle, std::string *reason )
{
    // pthread_t is an opaque type whose size varies (a pointer on Linux and macOS, a
    // struct on some older libraries), so it is heap-allocated and the Thread's
    // storage holds the pointer.  One allocation per thread, in a function that is
    // about to create a thread: the thread is the expensive part, not this.
    pthread_t *id = new pthread_t;

    const int rc = pthread_create( id, 0, &entry, &self );
    if( rc != 0 )
    {
        delete id;
        if( reason )
            *reason = std::string( "cannot start a thread: " )
                      + std::strerror( rc );
        return false;
    }

    handle[ 0 ] = id;
    handle[ 1 ] = 0;
    return true;
}

void threadPlatformJoin( void **handle )
{
    if( handle[ 0 ] == 0 )
        return;
    pthread_t *id = static_cast< pthread_t * >( handle[ 0 ] );

    // Joining is what makes the task safe to destroy, so the wait is indefinite on
    // purpose.  A task that never returns is a bug in the task, and hiding it behind
    // a timeout here would turn a hang that reproduces into a use-after-free that
    // does not.
    pthread_join( *id, 0 );
    delete id;
    handle[ 0 ] = 0;
}

} // namespace atui::core
} // namespace atui

#endif // ATUI_HAVE_THREADS && POSIX
