// A reference-counted pointer, for the one standard library without one.
//
// Watcom 1.9's <memory> declares std::auto_ptr and nothing else (measured on the
// box: shared_ptr is not in that header).  atui needs shared ownership in exactly
// one place -- the approval handshake, where a worker thread makes a request, the
// UI thread answers it, and neither of them may be the one that frees it -- so
// this is the type approval.h uses on that compiler.  Where the library has
// std::shared_ptr, that is what is used instead: it is the right type, and this
// is only a stand-in for the compiler that has never heard of it.
//
// The count is a plain long.  That is enough for Watcom because that build has no
// threads: compat/os2/include/README.md says why <thread> is not there yet and
// why the OS/2 network layer is being written as a state machine inside the event
// loop instead.  If that changes -- a second thread holding a copy of the same
// Ref -- this counter has to become the kernel's, and the handshake around it is
// worth re-reading at the same time: the mutex the handshake already has guards
// the request, not this pointer.

#ifndef ATUI_CORE_REF_H
#define ATUI_CORE_REF_H

namespace atui {
namespace core {

template< class T >
class Ref {
public:
    typedef T element_type;

    // Takes ownership.  A plain pointer, as std::shared_ptr's own constructor is:
    // `Ref<T>( new T )`, no factory function needed.
    explicit Ref( T *p = 0 )
        : p_( p ), count_( p ? new long( 1 ) : 0 )
    {
    }

    Ref( const Ref &o )
        : p_( o.p_ ), count_( o.count_ )
    {
        if( count_ )
            ++( *count_ );
    }

    ~Ref()
    {
        // The object goes before the count that was keeping track of it, and the
        // count after, so that a destructor that reads its own use_count() -- the
        // approval handshake does, in a log line -- is still looking at a live
        // counter.
        if( count_ && 0 == --( *count_ ) ) {
            delete p_;
            delete count_;
        }
    }

    Ref &operator=( const Ref &o )
    {
        if( this != &o ) {
            T *old = p_;
            long *oldCount = count_;
            p_ = o.p_;
            count_ = o.count_;
            if( count_ )
                ++( *count_ );
            if( oldCount && 0 == --( *oldCount ) ) {
                delete old;
                delete oldCount;
            }
        }
        return *this;
    }

    T *get() const { return p_; }
    T *operator->() const { return p_; }
    T &operator*() const { return *p_; }

    // The pre-C++11 way to be testable in an `if`: a pointer, so that `if (req)`
    // works and `if (req == 7)` does not obviously.
    operator void *() const { return static_cast<void *>( p_ ); }

    void reset()
    {
        Ref empty;
        swap( empty );
    }

    long use_count() const { return count_ ? *count_ : 0; }

    void swap( Ref &o )
    {
        T *p = p_;        p_ = o.p_;        o.p_ = p;
        long *c = count_; count_ = o.count_; o.count_ = c;
    }

private:
    T *p_;
    long *count_;
};

template< class T >
bool operator==( const Ref< T > &a, const Ref< T > &b )
{
    return a.get() == b.get();
}

template< class T >
bool operator!=( const Ref< T > &a, const Ref< T > &b )
{
    return a.get() != b.get();
}

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_REF_H
