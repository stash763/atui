/* mutex -- <mutex> for the era Windows build.

   Borland C++ 5.02 has no <mutex>: the include is answered with "Unable to open
   include file 'mutex.h'", and eight of atui's files ask for it.  This is the
   same hole the OS/2 build fills in compat/os2/include, and it is filled the
   same way -- the subset atui uses, in namespace std, over the operating
   system's own object, with the deviations written down rather than smoothed
   over.  Why adding to namespace std is the right shape for it is argued once,
   in that directory's README.md, and the argument is not compiler-specific.

   What is implemented:

      std::mutex              lock(), unlock(), try_lock()
      std::lock_guard< M >    ctor( M & ), dtor
      std::unique_lock< M >   ctor( M & ), lock(), unlock(), owns_lock()

   No recursive_mutex, no scoped_lock, no call_once: a call to one of those is a
   compile error rather than a substitute.

   The object is a Win32 kernel semaphore created with an initial and a maximum
   count of one.  Two alternatives were passed over, both measured:

   CRITICAL_SECTION is the fast answer and the one a Windows programmer expects,
   but TryEnterCriticalSection is not declared at this installation's default
   WINVER of 0x0400 -- "Call to undefined function" -- so try_lock() would mean
   raising WINVER and promising more of the API than the build needs.

   CreateMutexA would be the direct answer and is what an OS/2 mutex semaphore
   corresponds to, but a kernel mutex is recursive: the same thread taking it
   twice succeeds, where std::mutex is undefined and every implementation
   everyone else uses deadlocks.  A counting semaphore cannot do that -- a second
   WaitForSingleObject by the owner blocks -- so this header is more like the
   standard than the OS/2 one is, and a program that double-locks hangs here as
   it hangs elsewhere instead of quietly continuing.

   The cost of the kernel object over a CRITICAL_SECTION is a system call per
   lock.  atui locks eight mutexes and they guard the undo journal, the tool
   registry and the approval handshake: none of them is taken in a loop that a
   486 would notice.
*/
#ifndef ATUI_BCC_COMPAT_MUTEX_H
#define ATUI_BCC_COMPAT_MUTEX_H

#include <windows.h>
#include <stdlib.h>

namespace std {

class mutex {
public:
    mutex()
        : handle_( 0 )
    {
        handle_ = ::CreateSemaphoreA( 0, 1, 1, 0 );
        if ( !handle_ )
            ::abort();
    }

    ~mutex()
    {
        if ( handle_ )
            ::CloseHandle( handle_ );
    }

    void lock()    { ::WaitForSingleObject( handle_, INFINITE ); }
    void unlock()  { ::ReleaseSemaphore( handle_, 1, 0 ); }
    bool try_lock(){ return WAIT_OBJECT_0 == ::WaitForSingleObject( handle_, 0 ); }

private:
    mutex( const mutex & );
    mutex &operator=( const mutex & );

    HANDLE handle_;
};

template< class M >
class lock_guard {
public:
    explicit lock_guard( M &m )
        : mutex_( m )
    {
        mutex_.lock();
    }

    ~lock_guard()
    {
        mutex_.unlock();
    }

private:
    lock_guard( const lock_guard & );
    lock_guard &operator=( const lock_guard & );

    M &mutex_;
};

/* Unlike lock_guard this one records whether it is holding the mutex, because it
   is what condition_variable::wait() releases and reclaims around a wait. */
template< class M >
class unique_lock {
public:
    explicit unique_lock( M &m )
        : mutex_( m ), owns_( false )
    {
        lock();
    }

    ~unique_lock()
    {
        if ( owns_ )
            mutex_.unlock();
    }

    void lock()
    {
        mutex_.lock();
        owns_ = true;
    }

    void unlock()
    {
        mutex_.unlock();
        owns_ = false;
    }

    bool owns_lock() const { return owns_; }

private:
    unique_lock( const unique_lock & );
    unique_lock &operator=( const unique_lock & );

    M    &mutex_;
    bool  owns_;
};

} // namespace std

#endif /* ATUI_BCC_COMPAT_MUTEX_H */
