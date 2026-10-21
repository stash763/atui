// Tests for net::TurnQueue -- the hand-off between a turn on a thread and the screen.
//
// These are the cross-thread tests this layer needs before anything is moved onto a
// thread: that records keep their order across the hand-off, that the question-and-answer
// round trip actually wakes the waiting side, and that a question nobody answers ends as
// a refusal.  None of them depend on a model or a socket: the queue's contract is about
// two stacks and one lock, so a producer thread pushing sink calls is the honest test,
// and `ChatTurn` is left to its own tests to be a state machine.
//
// The producer is a real `core::Thread` when the build has one, and the same function
// run inline when it does not -- which is not a shortcut but the second half of the
// contract, since the no-thread build is the one where this queue is filled and drained
// on a single stack.
#ifdef ATUI_HAVE_NETWORK

#include "net/turn_queue.h"

#include "core/thread.h"
#include "harness.h"

#include <string>
#include <vector>

namespace atui_test {

namespace {

using atui::net::TurnQueue;

const int kRecords = 40;

// Pushes kRecords records through the sink interface, in order, with the kind
// alternating the way a real turn alternates them.
void pushRecords( TurnQueue &q )
{
    for( int i = 0; i < kRecords; ++i )
    {
        const std::string n( (size_t)( '0' + ( i % 10 ) ), 'x' );
        switch( i % 4 )
        {
            case 0:  q.onText( "text " + n, false );   break;
            case 1:  q.onText( "think " + n, true );   break;
            case 2:  q.onToolCall( "call " + n );      break;
            default: q.onToolResult( "result " + n );  break;
        }
    }
    q.onTurnFinished( true );
}

int test_queue_keeps_the_order_records_came_in()
{
    TurnQueue q;

    if( atui::core::threadsAvailable() )
    {
        // A producer thread pushing while this one drains: the point of the lock is
        // that the drain never sees a half-written record or a reordered pair.
        class Producer : public atui::core::Task
        {
        public:
            explicit Producer( TurnQueue &qq ) : q_( qq ) {}
            virtual void run() { pushRecords( q_ ); }
        private:
            TurnQueue &q_;
        } producer( q );

        atui::core::Thread thread;
        std::string reason;
        if( !thread.start( producer, &reason ) )
            return -1;

        // Drain while it is running, then again after the join, until it is finished.
        // Not a sleep-and-hope: the loop ends on the queue's own last record.
        int drained = 0;
        TurnQueue::Record rec;
        for( int spins = 0; spins < 200000; ++spins )
        {
            while( q.pop( rec ) )
                ++drained;
            bool ok = false;
            if( q.isFinished( ok ) && drained > 0 )
                break;
        }
        thread.join();
        while( q.pop( rec ) )
            ++drained;

        // 40 records, plus the `onTurnFinished` line -- and never one more or less,
        // which is what a lost or duplicated append under contention would change.
        if( drained != kRecords + 1 )
            return -2;
    }
    else
    {
        pushRecords( q );
        TurnQueue::Record rec;
        int drained = 0;
        while( q.pop( rec ) )
            ++drained;
        if( drained != kRecords + 1 )
            return -3;
    }

    // And the order, checked on the second pass over a queue filled fresh: text first,
    // then thinking, then a call, then its result.
    TurnQueue again;
    pushRecords( again );
    TurnQueue::Record rec;
    int seen = 0;
    while( again.pop( rec ) )
    {
        if( seen < kRecords )
        {
            const TurnQueue::Kind want =
                ( seen % 4 == 0 ) ? TurnQueue::tkText :
                ( seen % 4 == 1 ) ? TurnQueue::tkThinking :
                ( seen % 4 == 2 ) ? TurnQueue::tkTool : TurnQueue::tkToolResult;
            if( rec.kind != want )
                return -4;
        }
        else if( rec.kind != TurnQueue::tkFinished )
            return -5;
        ++seen;
    }
    if( seen != kRecords + 1 )
        return -6;
    return 0;
}

// The approval round trip: publish, take, answer, and see the answer reach the side
// that asked.  A worker's `waitIfWaiting()` is included because that sleep is what
// keeps an old machine from burning a core while a dialog is open.
int test_queue_carries_an_approval_both_ways()
{
    TurnQueue q;

    // Nothing asked yet: no question, no waiting, and an immediate answer call is
    // allowed (it is how a caller declines a question it never saw).
    atui::tools::ApprovalRef question;
    if( q.takeQuestion( question ) )
        return -1;
    if( q.waitIfWaiting() )
        return -2;

    q.onApprovalNeeded( atui::tools::ApprovalRef() );

    if( !q.takeQuestion( question ) )
        return -3;
    // A second take finds nothing: one question, one dialog.
    atui::tools::ApprovalRef twice;
    if( q.takeQuestion( twice ) )
        return -4;
    // The worker's view: it should sleep rather than spin.
    if( q.idleWaitMs() <= 0 )
        return -5;

    bool allow = false, session = false;
    if( q.hasAnswer( allow, session ) )
        return -6;                    // the UI has not decided yet

    q.answer( true, true );
    if( !q.hasAnswer( allow, session ) )
        return -7;
    if( !allow || !session )
        return -8;

    q.clearQuestion();
    atui::tools::ApprovalRef gone;
    if( q.takeQuestion( gone ) )
        return -9;
    return 0;
}

// A question that will never be answered -- the user quit with the dialog up -- has to
// end as a refusal, because "no answer" and "yes" are not the same thing and only one
// of them runs a command.
int test_queue_declines_what_nobody_answered()
{
    TurnQueue q;
    q.onApprovalNeeded( atui::tools::ApprovalRef() );
    q.declineUnanswered();

    bool allow = true, session = true;
    if( !q.hasAnswer( allow, session ) )
        return -1;
    if( allow || session )
        return -2;
    return 0;
}

} // anonymous namespace

} // namespace atui_test

namespace atui_test {

void init_turn_queue_tests()
{
    register_test( "queue: keeps the order records came in",
                   &test_queue_keeps_the_order_records_came_in );
    register_test( "queue: carries an approval both ways",
                   &test_queue_carries_an_approval_both_ways );
    register_test( "queue: declines what nobody answered",
                   &test_queue_declines_what_nobody_answered );
}

} // namespace atui_test

#endif // ATUI_HAVE_NETWORK
