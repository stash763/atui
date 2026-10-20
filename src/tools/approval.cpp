// Approval handshake + process-wide approver. See approval.h.
#include "tools/approval.h"

#include <chrono>

namespace atui {
namespace tools {

namespace {

// "Has the answer come in?"  The predicate forms of wait() are templates, so on
// a compiler without lambdas the predicate is a function object; it borrows the
// flag rather than the whole Approval, which is all it has to look at.  See
// compat/os2/include/condition_variable for the wait this is used with.
struct Answered
{
    explicit Answered( const bool &flag ) : flag_( flag ) {}
    bool operator()( void ) const { return flag_; }

private:
    const bool &flag_;
};

} // anonymous namespace

bool Approval::waitFor( long timeoutMs )
{
    std::unique_lock< std::mutex > lock( m_ );
    if( timeoutMs > 0 )
    {
        return cv_.wait_for( lock, std::chrono::milliseconds( timeoutMs ),
                             Answered( answered_ ) )
               && allow_;
    }
    cv_.wait( lock, Answered( answered_ ) );
    return allow_;
}

namespace {
Approver *g_approver = 0;   // not owned; installed by the front end
} // anonymous namespace

void     setApprover( Approver *a ) { g_approver = a; }
Approver *approver()                { return g_approver; }

} // namespace atui::tools
} // namespace atui
