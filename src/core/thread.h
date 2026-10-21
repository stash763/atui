// A second place to run work, on the platforms that have one.
//
// Why this exists: the screen must not stop while an HTTP response is waited for or
// while a tool runs a command.  The state machines (HttpStream, ChatRequest, ChatTurn)
// already never block the caller on the network -- they yield between bytes -- but a
// tool call is different: waiting for a child process has no non-blocking form on any
// platform atui runs on, so `run_command` blocks whatever runs it.  On a single stack
// that means the UI stops for the length of the command.  Moving that work to another
// thread is what the requirement needs, and PORTING.md's rule -- a state machine names
// no owner -- is what makes it a small change to the caller rather than a rewrite.
//
// What this is deliberately not: a thread *framework*.  There is no pool, no queue, no
// future, no promise, no condition variable in the interface, and nothing here knows
// what runs on the thread.  A thread is a scarce and expensive thing on these targets
// (every one costs a stack, and the interesting ones have single digits of memory to
// spare), so the abstraction stays the size of the need: start work, join it, ask
// whether the platform can do it at all.
//
// The last of those three is the important one.  `available()` is a compile-time
// answer, and every caller has to handle `false` by running the work on its own stack,
// because DOS has no threads and a cooperative scheduler is the answer there (PORTING.md).
// A caller that treats a refused `start()` as an error has turned a capability
// difference into a crash on the platform with the least to spare -- which is the same
// mistake as a build that assumes `fork()`.  The test suite runs both answers, so
// neither path is untested on the host.
//
// Measured, on the OS/2 target (`tools/os2/probe_thread.cpp`, numbers in TESTING.md):
// Watcom's `_beginthread` with `-bm` creates threads that run, and a child may use
// `malloc`/`free` and `printf`; TCP/IP works from a child too, which is the fact that
// decides whether an HTTP request can be pumped off the UI thread.  So the OS/2
// implementation below is code on top of something checked, not a guess about a
// library atui cannot test here.
#ifndef ATUI_CORE_THREAD_H
#define ATUI_CORE_THREAD_H

#include <string>

namespace atui {
namespace core {

// Something a Thread can run.  Not a function pointer and not a std::function: the
// work worth running off the UI thread is a state machine with state, and this build
// is C++98, where the portable way to say "an object with one method" is an interface.
struct Task
{
    virtual ~Task() {}

    // Runs on the other thread.  Two rules, both of them about the thing a thread
    // cannot do safely here:
    //
    //   * do not touch the screen.  The UI thread owns TVision, and tvision is not
    //     thread-safe; results come back through a queue the UI drains, never by
    //     drawing from here.
    //   * do not call `abort()` on the machine that started this thread, and do not
    //     hold a lock across a wait for the user: the UI thread may be the one
    //     waiting for the same lock to draw.
    virtual void run() = 0;
};

// Whether this build has threads at all.  A function rather than a macro at the call
// site, so the DOS case is a branch the compiler can see and a test can cover, and so
// the answer lives in one place when the third platform with a caveat turns up.
// Which builds have threads, decided by the platform macros and overridable by the
// build in one direction only.  `ATUI_NO_THREADS` is there so the no-thread case can
// be built and *tested* on a host that has threads: without it, the DOS answer -- the
// one platform where the fallback is not a contingency but the normal path -- would be
// the least tested code in the threading layer, and the Watcom build would only meet it
// on the target.
//
// Which builds have threads, decided where the platform macros are -- the same way
// net/sock.h decides which socket layer to use, with one addition: OS/2 is only a
// "yes" when the build also chose the multithreaded C runtime (`-bm`), because a
// thread created against a single-threaded runtime corrupts the heap and shows up
// weeks later somewhere else.  A compiler macro cannot see that decision, so the
// build says so; the target's own macros cannot be trusted to.
#if defined( __OS2__ )
#  ifdef ATUI_HAVE_OS2_THREADS
#    define ATUI_HAVE_THREADS 1
#  endif
#elif defined( __MSDOS__ ) || defined( __DOS__ )
   // DOS: no threads.  The answer is the cooperative one -- several state machines,
   // one stack, a timer tick deciding who steps next -- which needs no thread and no
   // emulation of one (PORTING.md, "Where the work runs").
#elif defined( _WIN32 )
#  define ATUI_HAVE_THREADS 1
#else
   // POSIX threads: Linux, FreeBSD, and the host every test runs on.  An unknown
   // platform that lacks <pthread.h> finds out at the compile, which is the loud
   // answer; guessing "no threads" instead would silently give every user a blocking
   // UI and nobody would think to look.
#  define ATUI_HAVE_THREADS 1
#endif

#ifdef ATUI_NO_THREADS
#  undef ATUI_HAVE_THREADS
#endif

bool threadsAvailable();

// One thread.  Not copyable, not reusable: a finished thread is a closed deal, and
// starting a second one is constructing a second object, which is the honest shape
// when each thread costs a stack.
class Thread
{
public:
    Thread();
    ~Thread();

    // Starts `task.run()` on a new thread.  Returns false when this build has no
    // threads, when the platform refused (out of memory is the normal reason and the
    // caller's "run it here instead" answer is the right one), or when this object is
    // already running a task.  `reason` explains a failure to the user.
    //
    // The task must outlive the thread: it is not copied, and the thread runs a
    // pointer to it.  The caller keeps it, because the caller joins.
    bool start( Task &task, std::string *reason = 0 );

    // Waits for the thread to finish.  Blocks the caller, which is the point: after
    // join() the task is safe to destroy.  Calling it when nothing is running returns
    // immediately.
    void join();

    bool running() const { return running_; }

    Task *task_;

public:
    // The trampoline every platform entry point calls with the Thread as its one
    // argument: it runs the task and reports completion.  Public because the C thread
    // entry points are C functions and this build has no way to make a static member
    // a callable C function pointer -- which is the same reason the socket layer's
    // callbacks are free functions.
    static void runFor( void *self );

private:
    Thread( const Thread & );
    Thread &operator=( const Thread & );

    bool  running_;

    // Platform state, one opaque per-implementation pointer.  Held as void* so this
    // header stays includable without <pthread.h> or <os2.h> -- those headers are
    // fragile about what is included before them, and dragging either into every
    // translation unit that wants a thread is how a port dies by a thousand cuts.
    //
    // Two words: enough for an OS/2 handle plus an event semaphore handle, a Windows
    // HANDLE plus a thread id, or a pthread_t.
    void *platform_[ 2 ];
};

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_THREAD_H
