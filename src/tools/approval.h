// User approval for sensitive tools.
//
// The chat loop runs on a worker thread; the confirmation UI lives on the TUI
// event-loop thread. An `Approval` is the one-shot handshake between them: the
// worker fills in the request text, hands it to the Approver, and blocks until
// the UI records a decision (or a timeout elapses). The UI implementation (see
// src/tui/app.cpp) is installed process-wide via setApprover().
//
// C++14. Network-independent (the worker/UI split is real only in network
// builds, but the primitive has no networking dependency).
#ifndef ATUI_TOOLS_APPROVAL_H
#define ATUI_TOOLS_APPROVAL_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

namespace atui {
namespace tools {

// One confirmation request. Created by the worker thread, answered by the UI.
class Approval {
public:
    Approval() : answered_( false ), allow_( false ) {}

    // Filled by the worker before the request is shown.
    std::string tool;      // tool name, e.g. "run_command"
    std::string summary;   // human-readable preview of the call

    // UI thread: record the decision and wake any blocked worker.
    void answer( bool allow )
    {
        {
            std::lock_guard< std::mutex > lock( m_ );
            allow_    = allow;
            answered_ = true;
        }
        cv_.notify_all();
    }

    // Worker thread: block until answered. A timeout (or any failure to get an
    // answer) is treated as a denial, so a stuck UI never runs a tool.
    bool waitFor( long timeoutMs );

    bool answered() const
    {
        std::lock_guard< std::mutex > lock( m_ );
        return answered_;
    }

private:
    mutable std::mutex m_;
    std::condition_variable cv_;
    bool answered_;
    bool allow_;
};

typedef std::shared_ptr< Approval > ApprovalRef;

// Implemented by the front end (the TUI). `approve` is called on the worker
// thread and must block until the user decides (typically it forwards the
// request to the UI thread and waits on `req->waitFor(...)`).
class Approver {
public:
    virtual ~Approver() {}
    virtual bool approve( const ApprovalRef &req ) = 0;
};

// Process-wide approver (not owned). The TUI installs it on startup; it stays
// null in headless/tests unless they set one. Null means "no way to ask" and
// the loop then refuses approval-requiring tools.
void   setApprover( Approver *approver );
Approver *approver();

} // namespace atui::tools
} // namespace atui

#endif // ATUI_TOOLS_APPROVAL_H
