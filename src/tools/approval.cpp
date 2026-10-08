// Approval handshake + process-wide approver. See approval.h.
#include "tools/approval.h"

#include <chrono>

namespace atui {
namespace tools {

bool Approval::waitFor( long timeoutMs )
{
    std::unique_lock< std::mutex > lock( m_ );
    if( timeoutMs > 0 )
    {
        return cv_.wait_for( lock, std::chrono::milliseconds( timeoutMs ),
                             [ this ] { return answered_; } )
               && allow_;
    }
    cv_.wait( lock, [ this ] { return answered_; } );
    return allow_;
}

namespace {
Approver *g_approver = 0;   // not owned; installed by the front end
} // anonymous namespace

void     setApprover( Approver *a ) { g_approver = a; }
Approver *approver()                { return g_approver; }

} // namespace atui::tools
} // namespace atui
