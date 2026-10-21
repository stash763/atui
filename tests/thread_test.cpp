// Tests for core/thread: work moves to another stack where one exists, and the refusal
// where it does not is honest enough for the caller to rely on.
//
// Two rules for anything added here, both because a thread test's failure mode is a
// hung suite rather than a red line:
//
//   * never sleep for a fixed time and hope.  Every wait is on a predicate and bounded,
//     so a bug shows up as a failure with a number attached.
//   * a build with no threads still asserts something.  `threadsAvailable()` being
//     false is a promise about `start()` -- false, with a reason, and the task not
//     touched -- and that is what these tests check on DOS and on the Watcom build
//     today.  Skipping silently would leave the platform that needs the fallback most
//     with no coverage of it at all.
#include "core/thread.h"

#include "harness.h"

#include <condition_variable>
#include <mutex>
#include <string>

namespace atui_test {

namespace {

using atui::core::Task;
using atui::core::Thread;
using atui::core::threadsAvailable;

// Waits for a predicate with a deadline, so a thread that never arrives is a failed
// test rather than a stopped run.  Returns the predicate's value at the deadline.
template< class Pred >
bool waitFor( std::mutex &m, std::condition_variable &cv, Pred pred, int limitMs = 5000 )
{
    std::unique_lock< std::mutex > lock( m );
    return cv.wait_for( lock, std::chrono::milliseconds( limitMs ), pred );
}

// Runs a computation and publishes it.  Reading `value` after join() is what the join
// is for: without it there is no happens-before edge and the test would be reading a
// race, not a result.
class Computation : public Task
{
public:
    Computation() : value_( 0 ) {}

    virtual void run() { value_ = 42; }

    int value() const { return value_; }

private:
    int value_;
};

int test_thread_runs_a_task_and_join_waits_for_it()
{
    Computation work;
    Thread thread;
    std::string reason;

    if( !thread.start( work, &reason ) )
    {
        // The other half of the contract: a build without threads says so, and the
        // caller does the work itself, which is exactly what the DOS path will do.
        if( threadsAvailable() )
            return -1;
        if( reason.empty() )
            return -2;
        work.run();
    }
    else
        thread.join();

    if( work.value() != 42 )
        return -3;
    if( thread.running() )
        return -4;
    return 0;
}

// The mutex is the other thing a threaded host depends on: not this test's arithmetic,
// but the fact that two threads adding under one lock cannot lose an update.  It is a
// dull test on a host and a real one on OS/2, where the mutex is atui's own header
// over DosRequestMutexSem.
class Adder : public Task
{
public:
    Adder( std::mutex &m, int &total, int steps )
        : m_( m ), total_( total ), steps_( steps ) {}

    virtual void run()
    {
        for( int i = 0; i < steps_; ++i )
        {
            std::lock_guard< std::mutex > lock( m_ );
            total_ += 1;
        }
    }

private:
    std::mutex &m_;
    int        &total_;
    int         steps_;
};

int test_thread_two_threads_do_not_lose_updates()
{
    if( !threadsAvailable() )
        return 0;                    // no threads: nothing to contend, and the
                                     // fallback has nothing to prove about locking

    const int kSteps = 2000;
    std::mutex m;
    int total = 0;
    Adder a( m, total, kSteps );
    Adder b( m, total, kSteps );

    Thread ta, tb;
    std::string reason;
    if( !ta.start( a, &reason ) ) return -1;
    if( !tb.start( b, &reason ) ) return -2;
    ta.join();
    tb.join();

    if( total != 2 * kSteps )
        return -3;                   // an update went missing
    return 0;
}

// A thread that is asked for twice must say no while it is busy, or two callers end up
// stepping the same state machine from two stacks -- which is the failure PORTING.md
// rules out, and the one every later layer would have to assume away.
//
// The task signals that it has started, so the second `start()` is asked while it is
// certainly running: no sleep, no race, no flake.
class Held : public Task
{
public:
    Held() : started_( false ), release_( false ), ran_( false ) {}

    virtual void run()
    {
        {
            std::lock_guard< std::mutex > lock( m_ );
            started_ = true;
        }
        cv_.notify_all();

        if( !waitFor( m_, cv_, [this] { return release_; } ) )
        {
            // Released too late to matter: report it by never setting `ran_`, which
            // the test below checks, rather than passing as though it had finished.
            return;
        }

        {
            std::lock_guard< std::mutex > lock( m_ );
            ran_ = true;
        }
        cv_.notify_all();
    }

    // The test waits on these, which is the whole point of the helper: the predicate
    // is evaluated under the same lock the task changes the flag under.
    std::mutex &mutex() { return m_; }
    std::condition_variable &cv() { return cv_; }

    void release()
    {
        {
            std::lock_guard< std::mutex > lock( m_ );
            release_ = true;
        }
        cv_.notify_all();
    }

    bool started() const { return started_; }
    bool ran() const { return ran_; }

private:
    std::mutex m_;
    std::condition_variable cv_;
    bool started_, release_, ran_;

    // Not copied, and the copy would not compile: a task owns its own waiting.
    Held( const Held & );
    Held &operator=( const Held & );
};

int test_thread_refuses_a_second_start_while_running()
{
    if( !threadsAvailable() )
        return 0;

    Held held, refused;
    Thread thread;
    std::string reason;

    if( !thread.start( held, &reason ) )
        return -1;
    if( !waitFor( held.mutex(), held.cv(), [p = &held] { return p->started(); } ) )
        return -2;                   // the thread never got as far as saying hello

    std::string second;
    const bool again = thread.start( refused, &second );

    held.release();                  // always let it finish: a leaked thread here
    thread.join();                   // hangs the suite instead of failing it

    if( again )
        return -3;
    if( second.empty() )
        return -4;
    if( !held.ran() )
        return -5;
    return 0;
}

} // anonymous namespace

} // namespace atui_test

namespace atui_test {

void init_thread_tests()
{
    register_test( "thread: runs a task and join waits for it",
                   &test_thread_runs_a_task_and_join_waits_for_it );
    register_test( "thread: two threads do not lose updates",
                   &test_thread_two_threads_do_not_lose_updates );
    register_test( "thread: refuses a second start while running",
                   &test_thread_refuses_a_second_start_while_running );
}

} // namespace atui_test
