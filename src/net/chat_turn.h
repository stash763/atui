// A chat turn -- the model, the tools and the asking -- as a state machine.
//
// One turn is: send the conversation plus what the user just typed; stream the
// answer; if the model asked for tools, decide about each one (the policy from the
// project's boundaries, a second model for the commands the boundaries cannot settle,
// and the user for everything else); run the ones that were cleared; send the
// results back and ask again, up to a limit, until the model gives an answer instead
// of a request.
//
// That is the sequence the threaded client used to run, written as a state machine
// instead: `start()` begins it, `step(waitMs)` advances it, and
// everything the user sees goes through `Sink`.  Two reasons, one of them permanent.
// A state machine does not care who pumps it -- the TUI's idle handler, a CLI caller
// willing to wait, a DOS program doing several things on one stack from a timer hook,
// or a worker thread on the platforms that have threads -- so there is one code path
// and one set of tests, and they mean the same thing on every target.  The other
// reason is mundane: the OS/2 build has no <thread> -- compat/os2/include gives it a
// working <mutex> and deliberately not a thread to need one for.  Nothing below
// assumes the calling thread, and nothing forbids one (see PORTING.md).
//
// Nothing here blocks, with one exception and it is deliberate: a tool that runs a
// command runs it in the step that was told to, because waiting for a child process
// is the one thing that has no non-blocking form on the platforms atui runs on (see
// the note in `run_command_tool.cpp` and the platform table in TESTING.md).  The
// network and the judging yield between bytes.
//
// A tool call is not executed in the same moment it is decided.  `tsRunningTool`
// means "next `step()` will", which is the gap the screen uses to show the call
// before a command that may take a minute has started: with a blocking child process,
// that single record on screen is what tells the user the program is working and not
// wedged.
//
// The conversation is committed only when the model answered.  A turn that failed
// halfway -- refused, timed out, lost the connection -- leaves the stored history
// exactly as it was, so the next message is asked against what the model actually
// saw rather than against an exchange it never received.
#ifndef ATUI_NET_CHAT_TURN_H
#define ATUI_NET_CHAT_TURN_H

#include "chat_request.h"
#include "oai_body.h"

#include "tools/approval.h"

#include <string>
#include <vector>

namespace atui {
namespace net {

class ChatTurn : public ChatRequest::Sink
{
public:
    // Everything the turn says, said to whoever is drawing.  Each method is called
    // from inside `step()`, so it may draw and must not block.
    class Sink
    {
    public:
        virtual ~Sink() {}
        // Part of the answer, or of its reasoning.
        virtual void onText( const std::string &text, bool thinking ) = 0;
        // A line about what is happening or what went wrong: not part of the
        // answer, and not replayed to the model.
        virtual void onStatus( const std::string &text ) = 0;
        // A tool call, recorded the moment it is made so a slow command is visible
        // while it runs, and a note about who cleared it.
        virtual void onToolCall( const std::string &preview ) = 0;
        virtual void onToolNote( const std::string &note ) = 0;
        // And its outcome, which closes the record.
        virtual void onToolResult( const std::string &result ) = 0;
        // The policy could not clear a call, so the user is asked.  Show the
        // question and answer it with `answerApproval()`; the turn waits -- drawing
        // -- until it is answered, and no answer at all means no.
        virtual void onApprovalNeeded( const atui::tools::ApprovalRef &question ) = 0;
        // The turn is over: answered, refused or failed.  Called once.
        virtual void onTurnFinished( bool ok ) = 0;
    };

    ChatTurn();

    // Starts a turn.  Returns false if one is already running -- a second message
    // mid-turn is a thing users do, and refusing it is better than two conversations
    // interleaving.
    bool start( const std::string &userMessage, Sink *sink,
                int connectSeconds = 30, int idleSeconds = 0 );

    // Advances the turn: pumps whichever request is live, or runs the tool that was
    // cleared and is waiting for its turn.  Returns having done at most one of
    // those, so the caller keeps the screen.
    void step( int waitMs );

    bool busy() const { return state_ != tsIdle && state_ != tsDone; }

    // The user's answer to `onApprovalNeeded`.  Late answers (after the turn ended
    // or was cancelled) are ignored rather than applied to whatever runs next.
    void answerApproval( bool allow, bool allowSession );

    // Ends the turn now (escape pressed).  A tool already running is not interrupt-
    // able; the turn stops after it.
    void cancel( const std::string &why );

    // The conversation as the model has seen it: only complete turns are in it.
    const std::vector< OaiMessage > &conversation() const { return conv_; }
    std::size_t messageCount() const { return conv_.size(); }
    void clearConversation() { conv_.clear(); }

private:
    enum State
    {
        tsIdle,              // built, never started
        tsAsking,            // a model request is in flight
        tsJudging,           // the permission agent is describing a call
        tsAwaitingApproval,  // the user has the question
        tsRunningTool,       // cleared (or refused): the next step deals with it
        tsDone               // finished; conv_ committed if it ended well
    };

    // ChatRequest::Sink.  Which of the two live requests is speaking is known from
    // `state_`, so the judge's text never reaches the transcript by accident.
    virtual void onText( const std::string &text, bool thinking );
    virtual void onFinished( bool ok, const std::string &error );

    bool sendRound();                    // ask the model; false if it could not start
    void afterAnswer( bool ok, const std::string &error );
    void decideCurrentCall();            // policy, agent, or user for tool_
    void afterJudge( bool ok );          // the agent described it: decide again
    void askUser();                      // publish the question
    void runOrSkip();                    // the next step executes or reports
    void finishCall( const std::string &result );
    void finishTurn( bool ok );

    State state_;
    Sink *sink_;

    ChatRequest ask_;      // the conversation's model
    ChatRequest judge_;    // the permission agent, when one is needed

    std::vector< OaiMessage > msgs_;   // this turn, including what is not committed
    std::vector< OaiToolCall > pendingTools_;   // what the last answer asked for
    std::vector< OaiMessage > conv_;   // what the model has actually seen
    std::string toolsJson_;            // offered tools, rendered once per turn

    size_t toolIndex_;                 // which requested call is being handled
    bool skipRun_;                     // the result is already known (refusal)
    std::string pendingResult_;        // that result, when skipping
    std::string pendingNote_;          // who cleared the call, for the transcript
    std::string callName_;             // the call being dealt with
    std::string callArgs_;
    std::string judgeKey_;             // its plan cache key, while judging

    atui::tools::ApprovalRef question_;
    bool answered_;
    bool allow_;
    bool allowSession_;
    int round_;

    // Snapshot of the endpoint, so a dialog edit mid-turn cannot change the server
    // under a request that is halfway out of the door.
    std::string baseUrl_;
    std::string apiKey_;
    std::string model_;
    double temperature_;
    bool thinkingResend_;
    int connectSeconds_;
    int idleSeconds_;
};

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_CHAT_TURN_H
