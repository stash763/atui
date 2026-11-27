// Approval handshake + process-wide approver. See approval.h.
#include "tools/approval.h"
#include "core/int64.h"      // Int64: what a duration's count() answers with

#include <chrono>

namespace atui {
namespace tools {

namespace {

// "Has the answer come in?"  The predicate is a function object rather than a
// lambda because the era compilers are C++98; it borrows the flag rather than the
// whole Approval, which is all it has to look at.
struct Answered
{
    explicit Answered( const bool &flag ) : flag_( flag ) {}
    bool operator()( void ) const { return flag_; }

private:
    const bool &flag_;
};

// Waiting until the answer comes, in the open.  std::condition_variable's
// predicate forms are member functions that are templates, and Borland C++ 5.02
// has no member templates (measured; see compat/bcc/include/conditio.h), so the
// loops below are written the way [thread.condition.condvar] defines those forms
// rather than called.  Written once and used on every compiler, because a wait
// that is spelled two ways is a wait that can be wrong in two places -- and the
// important property, that the mutex is free while the thread is asleep and held
// when the answer is read, is easier to see in the open than inside a template.
// Did the wait end because something was posted?  The standard's two-argument
// wait_for answers that with std::cv_status; the implementation this file uses on
// Borland -- compat/bcc/include/conditio.h, because the compiler has no member
// templates and so cannot take the predicate form -- answers it with a bool, and
// `std::cv_status` is not a name in that library at all (measured).  Asked as one
// function, the loop below reads the same on every compiler and there is one line
// where the two answers are reconciled rather than one per wait.
inline bool notifiedInTime( std::condition_variable     &cv,
                            std::unique_lock< std::mutex > &lock,
                            core::Int64                  timeoutMs )
{
#if defined( __BORLANDC__ )
    return cv.wait_for( lock, std::chrono::milliseconds( timeoutMs ) );
#else
    return std::cv_status::no_timeout ==
           cv.wait_for( lock, std::chrono::milliseconds( timeoutMs ) );
#endif
}

template< class P >
void waitUntilAnswered( std::condition_variable &cv,
                        std::unique_lock< std::mutex > &lock,
                        P                                   answered )
{
    while ( !answered() )
        cv.wait( lock );
}

template< class P >
bool waitUntilAnsweredFor( std::condition_variable          &cv,
                           std::unique_lock< std::mutex >   &lock,
                           long                              timeoutMs,
                           P                                 answered )
{
    const std::chrono::steady_clock::time_point end =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs );

    for( ;; )
    {
        if( answered() )
            return true;

        const core::Int64 left =
            (core::Int64) std::chrono::duration_cast< std::chrono::milliseconds >(
                end - std::chrono::steady_clock::now() ).count();
        if( left <= 0 )
            return false;

        // A wake with no answer in it is allowed by the standard and means another
        // look; a wake with no timeout left means the prompt was never answered,
        // which is a denial.
        if( !notifiedInTime( cv, lock, left ) )
            return answered();
    }
}

} // anonymous namespace

bool Approval::waitFor( long timeoutMs )
{
    std::unique_lock< std::mutex > lock( m_ );
    if( timeoutMs > 0 )
    {
        return waitUntilAnsweredFor( cv_, lock, timeoutMs, Answered( answered_ ) )
               && allow_;
    }
    waitUntilAnswered( cv_, lock, Answered( answered_ ) );
    return allow_;
}

// Named rather than anonymous, and reopened for lookup on the line after
// the one that closes it, which is all an anonymous namespace says:
// these names belong to this file and are visible to the rest of it
// from here on.  The spelling has to be written out because Borland
// 5.02 refuses a second anonymous namespace inside a named one
// ("Multiple declaration for 'atui::tools::<unnamedNS>'", measured, in any
// named namespace, while two at global scope are accepted) and this
// file has more than one section.
namespace approval_local {
Approver *g_approver = 0;   // not owned; installed by the front end
} // namespace approval_local
using namespace approval_local;

void     setApprover( Approver *a ) { g_approver = a; }
Approver *approver()                { return g_approver; }

} // namespace atui::tools
} // namespace atui
