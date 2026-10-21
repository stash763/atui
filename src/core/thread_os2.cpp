// OS/2 threads with Open Watcom: `_beginthread` for the thread, and an event
// semaphore for the join, because the C runtime gives you no way to wait.
//
// Requires `-bm` (and therefore `ATUI_HAVE_OS2_THREADS`, which the header turns into
// `ATUI_HAVE_THREADS`): `_beginthread` against the single-threaded runtime creates
// something that runs but shares the heap's bookkeeping with the thread that made it.
// Measured on the target with `tools/os2/probe_thread.cpp` (TESTING.md has the
// numbers): with `-bm`, two threads and the parent all run, `malloc`/`free` in a child
// works, `printf` works, and a socket opened in a child talks to the network.
//
// Why not `DosCreateThread`, which also measured working: it is the kernel's call and
// knows nothing about the C runtime's per-thread start-up, so the child gets a heap
// that was never initialised for a second thread.  `printf` happened to survive that in
// the probe; `std::string` is not going to be so forgiving, and "usually works" is not
// an answer for a use-after-corruption.  `_beginthread` is the wrapper that does the
// runtime's setup, so it is the one this file uses.
#include "thread_platform.h"

#if defined( ATUI_HAVE_THREADS ) && defined( __OS2__ )

#ifndef INCL_DOS
#define INCL_DOS
#endif
#include <os2.h>

#include <process.h>

namespace atui {
namespace core {

namespace {

// A thread of atui's steps a state machine that holds sockets, strings and a parsed
// response, and on the machine this targets that is worth more than the 8 KB the
// default would give.  It is committed virtual memory, not real: the box cares about
// the difference and the number is not the reason to skimp.
const ULONG kStackBytes = 256UL * 1024UL;

// What the child needs to know.  Heap-allocated by the starter and freed by the
// joiner: the child's last act is to post the semaphore, and it touches nothing in
// here after that, so freeing once the wait returns cannot race with it.
struct Handoff
{
    void *self_;
    HEV   done_;
};

void child( void *arg )
{
    Handoff *h = static_cast< Handoff * >( arg );
    Thread::runFor( h->self_ );
    DosPostEventSem( h->done_ );
}

} // anonymous namespace

bool threadPlatformStart( Thread &self, void **handle, std::string *reason )
{
    Handoff *h = new Handoff;
    h->self_ = &self;
    h->done_ = 0;

    if( DosCreateEventSem( 0, &h->done_, 0UL, FALSE ) != 0 )
    {
        delete h;
        if( reason )
            *reason = "cannot create the semaphore a thread joins on";
        return false;
    }

    // `_beginthread( start, reserved, stack, arg )`: the probe used exactly this
    // shape and reported handles 2 and 3 for two threads.  -1 is the failure answer.
    if( _beginthread( &child, 0, (unsigned) kStackBytes, h ) == -1 )
    {
        DosCloseEventSem( h->done_ );
        delete h;
        if( reason )
            *reason = "cannot start a thread (out of memory or thread handles)";
        return false;
    }

    handle[ 0 ] = h;
    handle[ 1 ] = 0;
    return true;
}

void threadPlatformJoin( void **handle )
{
    if( handle[ 0 ] == 0 )
        return;
    Handoff *h = static_cast< Handoff * >( handle[ 0 ] );

    // Indefinite, for the same reason pthread_join is: the wait is what makes the task
    // safe to destroy.  Ctrl-C still reaches the process, and quitting with a thread
    // waiting on a child process is a thing the user does -- which is why a tool that
    // runs a command has its own deadline (see TESTING.md, "What a command atui runs
    // gets") rather than relying on this one.
    DosWaitEventSem( h->done_, SEM_INDEFINITE_WAIT );
    DosCloseEventSem( h->done_ );
    delete h;
    handle[ 0 ] = 0;
}

} // namespace atui::core
} // namespace atui

#endif // ATUI_HAVE_THREADS && __OS2__
