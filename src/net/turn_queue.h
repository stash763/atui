// The queue between a turn running on a thread and the screen that has to draw it.
//
// The rule this exists to enforce (PORTING.md): a sink is called on whatever stack is
// stepping the state machine, and it must not draw, because TVision belongs to the
// thread that owns the screen.  So when a `ChatTurn` runs on a worker, its sink cannot
// be the transcript.  It is this object: the worker appends, the UI drains, and the UI
// is the only thing that touches TVision.
//
// The same object serves the builds with no threads, which is the part that keeps the
// UI from having two versions of itself: on those platforms the turn is stepped on the
// UI's own timer and the queue is filled and drained on one stack.  The mutex is
// uncontended there and costs a few instructions; the alternative, a second code path
// for "am I threaded", is a second thing to test and the sort of thing that drifts.
//
// Approval is the interesting case, and the reason this is not just a vector with a
// lock.  The turn asks the user something from inside `step()` and then waits -- it has
// no other way to get an answer -- so:
//
//   * the worker puts the question in the slot and goes back to stepping, which returns
//     immediately while the turn is waiting (the pump in the worker sleeps on
//     `answerArrived()` rather than spinning);
//   * the UI drains the question, shows its dialog, and calls `answer()`, which answers
//     the turn and wakes the worker;
//   * if the UI never answers -- the user quit, the window closed -- the question is
//     `decline()`d, and a turn with no answer runs nothing, which is the safe direction.
//
// Records are drained in the order they were appended, and the drain happens under the
// same lock the appends happen under, so a tool call and its result cannot arrive
// reordered or interleaved with each other.  That ordering is the whole content of a
// transcript: `onToolCall` before `onToolResult`, the status lines between them saying
// who cleared it.
#ifndef ATUI_NET_TURN_QUEUE_H
#define ATUI_NET_TURN_QUEUE_H

#include "chat_turn.h"
#include "tools/approval.h"

#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

namespace atui {
namespace net {

class TurnQueue : public ChatTurn::Sink
{
public:
    enum Kind
    {
        tkText,        // part of the answer
        tkThinking,    // part of its reasoning
        tkStatus,      // a line about what is happening, or what went wrong
        tkTool,        // a tool call, recorded when it is made
        tkToolNote,    // who cleared it
        tkToolResult,  // and what came back
        tkFinished     // once, last
    };

    struct Record
    {
        Kind        kind;
        std::string text;

        Record() : kind( tkStatus ) {}
        Record( Kind k, const std::string &t ) : kind( k ), text( t ) {}
    };

    TurnQueue()
        : finished_( false )
        , ok_( false )
        , asked_( false )
        , answered_( false )
        , shown_( false )
        , allow_( false )
        , allowSession_( false )
    {
    }

    // ------------------------------------------------------------ the worker's side
    // ChatTurn::Sink.  Called on the thread stepping the turn.  None of them block
    // except `onApprovalNeeded`, and that one does not block either: it publishes the
    // question and returns, because the turn it came from is about to wait anyway and
    // holding the lock across a user's decision is how a UI deadlocks.

    virtual void onText( const std::string &text, bool thinking )
    {
        append( thinking ? tkThinking : tkText, text );
    }
    virtual void onStatus( const std::string &text )    { append( tkStatus, text ); }
    virtual void onToolCall( const std::string &text )  { append( tkTool, text ); }
    virtual void onToolNote( const std::string &text )  { append( tkToolNote, text ); }
    virtual void onToolResult( const std::string &text ){ append( tkToolResult, text ); }

    virtual void onApprovalNeeded( const atui::tools::ApprovalRef &question )
    {
        std::lock_guard< std::mutex > lock( m_ );
        question_ = question;
        asked_ = true;
        answered_ = false;
    }

    virtual void onTurnFinished( bool ok )
    {
        std::lock_guard< std::mutex > lock( m_ );
        ok_ = ok;
        finished_ = true;
        // Always a record, empty text or not: this is the one the UI closes the reply
        // block on, and dropping it because the message is empty is how a transcript
        // ends up with an answer that never finishes.  (The empty-text rule in
        // `appendLocked` is about streamed chunks, which really are noise.)
        finishedLocked( ok );
    }

    // --------------------------------------------------------------- the UI's side
    // Takes the next record, oldest first.  False when there is none, which is the
    // drain loop's end -- and it is deliberately not "the turn is over": a turn can go
    // on producing records after the last drain, and the UI decides how many to take
    // before it redraws.
    bool pop( Record &out )
    {
        std::lock_guard< std::mutex > lock( m_ );
        if( queue_.empty() )
            return false;
        out = queue_.front();
        queue_.erase( queue_.begin() );
        return true;
    }

    // The question the turn is waiting on, if it is waiting on one.  Taken rather than
    // peeked, so two drains cannot both show the same question.
    bool takeQuestion( atui::tools::ApprovalRef &out )
    {
        std::lock_guard< std::mutex > lock( m_ );
        if( !asked_ || answered_ || shown_ )
            return false;
        shown_ = true;               // one question, one dialog (the test that caught
                                     // this not doing it was right to be suspicious)
        out = question_;
        return true;
    }

    bool isFinished( bool &ok )
    {
        std::lock_guard< std::mutex > lock( m_ );
        ok = ok_;
        return finished_;
    }

    // How long the worker should sleep before stepping again.  A turn waiting on the
    // user wants a sleep and a wakeup, not a spin: the box is old and the alternative
    // is a thread at full tilt while a dialog is open.
    int idleWaitMs()
    {
        std::lock_guard< std::mutex > lock( m_ );
        return asked_ && !answered_ ? kWaitUserMs : 0;
    }

    // Wakes a worker sleeping in `waitForAnswer()`.  Returns false if there was nothing
    // to wait for, so a caller that would rather step than sleep can just step.
    bool waitIfWaiting()
    {
        std::unique_lock< std::mutex > lock( m_ );
        if( !( asked_ && !answered_ ) )
            return false;
        // Bounded, and the bound is the point: a worker that sleeps forever waiting for
        // a UI that has quit is a process that cannot be closed.  It wakes, steps (the
        // turn returns immediately while it is still waiting), and sleeps again, so the
        // only thing the timeout costs is a few wakeups.
        cv_.wait_for( lock, std::chrono::milliseconds( kWaitUserMs ) );
        return true;
    }

    // The UI has decided.  `answer` is what the turn gets, and the wake happens after
    // the answer is stored, so the worker cannot resume into a half-updated queue.
    void answer( bool allow, bool allowSession )
    {
        {
            std::lock_guard< std::mutex > lock( m_ );
            allow_ = allow;
            allowSession_ = allowSession;
            answered_ = true;
        }
        cv_.notify_all();
    }

    // Read by whoever drives the turn: the answer the UI left, and whether there is one.
    bool hasAnswer( bool &allow, bool &allowSession )
    {
        std::lock_guard< std::mutex > lock( m_ );
        if( !answered_ )
            return false;
        allow = allow_;
        allowSession = allowSession_;
        return true;
    }

    // A question the user is never going to answer: the app is closing, or the turn was
    // cancelled while the dialog was up.  Refusing is the only safe default, and it is
    // also what a turn does with no answer at all -- this exists so the worker is not
    // left waiting on a decision that has become impossible.
    void declineUnanswered()
    {
        answer( false, false );
    }

    void clearQuestion()
    {
        std::lock_guard< std::mutex > lock( m_ );
        asked_ = false;
        shown_ = false;
        question_.reset();
    }

private:
    // An enum, not a `static const int`, for a reason that took a link error to find:
    // `std::chrono::milliseconds( kWaitUserMs )` takes its argument by const reference,
    // which odr-uses the constant and demands an out-of-line definition of it that a
    // header-only class does not have.  An enum constant is a prvalue, so it binds to a
    // reference through a temporary and no symbol is needed.  It is also what the rest
    // of the tree does (`kMaxTests` in the test runner, `Cmd*` in the TUI).
    enum { kWaitUserMs = 50 };

    void append( Kind k, const std::string &text )
    {
        std::lock_guard< std::mutex > lock( m_ );
        appendLocked( k, text );
    }

    // Empty streamed chunks are dropped -- an SSE keepalive or an empty reasoning
    // delta is not a line in a transcript -- but every other kind is kept, including
    // an empty status, because the kinds that carry no text carry a fact.
    void appendLocked( Kind k, const std::string &text )
    {
        if( text.empty() && ( k == tkText || k == tkThinking ) )
            return;
        queue_.push_back( Record( k, text ) );
    }

    void finishedLocked( bool ok )
    {
        queue_.push_back( Record( tkFinished, ok ? "" : "the turn ended with an error" ) );
    }

    std::mutex              m_;
    std::condition_variable cv_;
    std::vector< Record >   queue_;

    bool  finished_;
    bool  ok_;

    // The approval slot: one question at a time, because a turn has one pending
    // decision at a time (`ChatTurn` handles calls one by one, see its `toolIndex_`).
    atui::tools::ApprovalRef question_;
    bool asked_, answered_, shown_;
    bool allow_, allowSession_;

    // Not copied: it holds a mutex, and a copy of a queue that somebody is appending
    // to is not a thing that means anything.
    TurnQueue( const TurnQueue & );
    TurnQueue &operator=( const TurnQueue & );
};

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_TURN_QUEUE_H
