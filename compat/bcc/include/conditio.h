/* conditio -- <condition_variable> for the era Windows build.

   The file is called conditio.h because that is what the compiler asks for: an
   include with no extension is truncated to eight characters before the .h is
   appended, so `#include <condition_variable>` looks for conditio.h and reports
   "Unable to open include file 'conditio.h'" when there is not one (measured: a
   condition_variable.h in the same directory, on the same include path, is not
   opened).  Renaming the file is the whole of the workaround, and any header
   added here with a name longer than eight characters needs the same treatment
   or it will sit unread.

   No <condition_variable> in Borland C++ 5.02, and one real user: the approval
   handshake, where the thread that wants to run a sensitive tool waits while the
   UI thread stays responsive enough to draw the question, and a timeout has to
   mean "refuse" -- a prompt nobody answered must never become a command that
   ran.

   What is implemented:

      notify_all(), notify_one()
      wait( unique_lock< mutex > & )
      wait_for( unique_lock< mutex > &, milliseconds )              -> bool

   What is not, and it is the one place this header is smaller than the OS/2
   one: the predicate forms.  They are member functions that are templates, and
   this compiler has no member templates -- none, in any class, in any form
   (measured: `template< class P > void f( P )` inside a class is "Declaration
   terminated incorrectly", while the same template as a free function compiles,
   so it is the membership rather than the template).  The one predicate wait in
   the program is in the approval handshake, and tools/approval.cpp now says the
   loop out loud, which is what [thread.condition.condvar] says the predicate
   form means.  One spelling on every compiler is also the smaller answer: there
   is then nothing to keep true between two implementations of the same wait.

   The object is a Win32 counting semaphore, created empty, and posting is
   ReleaseSemaphore.  What makes the lost wakeup impossible is that a post is
   never cleared: a ReleaseSemaphore that arrives after the predicate was found
   false and before the caller's mutex is handed back is kept by the kernel as a
   count, and the next wait takes it immediately.  That is why there is no
   counterpart to the reset the OS/2 header does before unlocking -- an event has
   to be cleared and a count does not -- and it is the reason this shape was
   chosen over the manual-reset event, where clearing and waiting are two
   operations and the gap between them is where the wakeup goes.

   The waiter count exists for notify_all().  Releasing one waiter when several
   are waiting is not allowed by the standard -- waking more is always allowed,
   waking fewer is not -- so the number of threads currently in a wait is kept,
   incremented while the caller's mutex is held and so sequenced against a notify
   that holds the same mutex.  When it is wrong it is wrong in the direction of
   over-posting, which shows up as a wait returning for no reason and being
   re-tested against the predicate: what a condition_variable is required to
   tolerate.

   There is no condition_variable with its own mutex, and no predicate-free
   guarantee against spurious wakeups: every wait form atui uses takes the
   predicate, and the ones that do not are documented as waking spuriously.
*/
#ifndef ATUI_BCC_COMPAT_CONDITION_VARIABLE_H
#define ATUI_BCC_COMPAT_CONDITION_VARIABLE_H

#include <chrono>
#include <mutex>
#include <windows.h>
#include <stdlib.h>

namespace std {

class condition_variable {
public:
    condition_variable()
        : handle_( 0 ), waiting_( 0 )
    {
        /* The maximum count only bounds how far ahead a post can run; nobody
           posts a thousand times before waiting. */
        handle_ = ::CreateSemaphoreA( 0, 0, 0x0FFFFFFF, 0 );
        if ( !handle_ )
            ::abort();
    }

    ~condition_variable()
    {
        if ( handle_ )
            ::CloseHandle( handle_ );
    }

    void notify_all()
    {
        const LONG n = ::InterlockedExchangeAdd( &waiting_, 0 );
        if ( n > 0 )
            ::ReleaseSemaphore( handle_, n, 0 );
    }

    void notify_one()
    {
        ::ReleaseSemaphore( handle_, 1, 0 );
    }

    void wait( unique_lock< mutex > &lock )
    {
        ::InterlockedIncrement( &waiting_ );
        lock.unlock();
        ::WaitForSingleObject( handle_, INFINITE );
        lock.lock();
        ::InterlockedDecrement( &waiting_ );
    }

    /* Without a predicate the answer is the kernel's: true when something was
       posted, false when the timeout went by.  The standard spells that answer
       std::cv_status, a name this library does not have (measured), so a caller
       that wants one spelling everywhere has to ask the question through a
       function of its own: tools/approval.cpp's notifiedInTime() is that caller,
       and the only one. */
    bool wait_for( unique_lock< mutex > &lock, const chrono::milliseconds &d )
    {
        ::InterlockedIncrement( &waiting_ );
        lock.unlock();
        const DWORD rc = ::WaitForSingleObject( handle_, millisecondsOf( d ) );
        lock.lock();
        ::InterlockedDecrement( &waiting_ );
        return WAIT_OBJECT_0 == rc;
    }

private:
    condition_variable( const condition_variable & );
    condition_variable &operator=( const condition_variable & );

    static DWORD millisecondsOf( const chrono::milliseconds &d )
    {
        const __int64 ms = d.count();
        if ( ms <= 0 )
            return 0;
        if ( ms > 0x0FFFFFFF )
            return INFINITE;
        return (DWORD) ms;
    }

    HANDLE handle_;
    LONG   waiting_;
};

} // namespace std

#endif /* ATUI_BCC_COMPAT_CONDITION_VARIABLE_H */
