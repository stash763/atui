// Definitions for the event-loop chat turn.  See chat_turn.h.
#include "chat_turn.h"

#include "permission_judge.h"
#include "provider.h"

#include "tools/judge.h"
#include "tools/permissions.h"
#include "tools/preview.h"
#include "tools/tool.h"
#include "tools/tool_registry.h"

#include "core/strutil.h"      // toString: Watcom has no std::to_string
#include "core/hostenv.h"      // hostEnvBrief: the system message

namespace atui {
namespace net {

namespace {

// A model that asks for tools six times without ever answering is not going to
// answer.  The limit is here so that one confused turn costs six requests rather than
// the session, and running out says so in the transcript: a turn that simply stops
// looks like the program gave up on its own.
const int kMaxToolRounds = 6;

} // anonymous namespace

ChatTurn::ChatTurn()
    : state_( tsIdle )
    , sink_( 0 )
    , toolIndex_( 0 )
    , skipRun_( false )
    , answered_( false )
    , allow_( false )
    , allowSession_( false )
    , round_( 0 )
    , temperature_( 0.0 )
    , thinkingResend_( false )
    , connectSeconds_( 30 )
    , idleSeconds_( 0 )
{
}

bool ChatTurn::start( const std::string &userMessage, Sink *sink,
                      int connectSeconds, int idleSeconds )
{
    if( busy() )
        return false;

    sink_ = sink;
    atui::tools::registerDefaults();

    // Snapshot the endpoint: a Providers dialog edit halfway through a turn must not
    // retarget a request that is already under way.  The next message picks the
    // change up.
    const ProviderSettings cfg = providerSettings();
    baseUrl_ = cfg.baseUrl;
    apiKey_ = cfg.apiKey;
    model_ = cfg.model;
    temperature_ = cfg.temperature;
    thinkingResend_ = cfg.thinkingResend;
    connectSeconds_ = connectSeconds > 0 ? connectSeconds : 30;
    idleSeconds_ = idleSeconds > 0 ? idleSeconds : 0;

    msgs_ = conv_;
    msgs_.push_back( OaiMessage( "user", userMessage ) );
    toolsJson_ = atui::tools::ToolRegistry::instance().renderToolsJson();
    pendingTools_.clear();
    toolIndex_ = 0;
    round_ = 1;
    return sendRound();
}

void ChatTurn::step( int waitMs )
{
    if( state_ == tsAsking )
    {
        ask_.step( waitMs );
        return;
    }
    if( state_ == tsJudging )
    {
        judge_.step( waitMs );
        return;
    }
    if( state_ == tsAwaitingApproval )
    {
        if( !answered_ )
            return;                    // the question is with the user; no rush
        answered_ = false;
        if( allow_ && allowSession_ )
            atui::tools::rememberAllowed( callName_, callArgs_ );
        pendingNote_ += allow_ ? " (you allowed it)" : " (you declined)";
        if( !allow_ )
        {
            skipRun_ = true;
            pendingResult_ = "The user declined to run '" + callName_ + "'.";
        }
        runOrSkip();
        return;
    }
    if( state_ == tsRunningTool )
    {
        // The note first, so the transcript says who cleared a call before its
        // output arrives -- and before a command that takes a minute has begun.
        if( !pendingNote_.empty() && sink_ )
            sink_->onToolNote( pendingNote_ );
        pendingNote_.clear();

        std::string result = pendingResult_;
        pendingResult_.clear();
        if( !skipRun_ )
        {
            atui::tools::Tool *tool =
                atui::tools::ToolRegistry::instance().find( callName_ );
            // Blocking on purpose, and the one place this design accepts it: see the
            // header.  A child process is reaped by the same execution stream that
            // draws the screen, on a platform with nothing else to do it with.
            result = tool ? tool->execute( callArgs_ )
                          : "error: tool '" + callName_ + "' disappeared";
        }
        skipRun_ = false;
        finishCall( result );
        return;
    }
}

void ChatTurn::answerApproval( bool allow, bool allowSession )
{
    if( state_ != tsAwaitingApproval )
        return;             // a late answer is not applied to the next call
    allow_ = allow;
    allowSession_ = allowSession;
    answered_ = true;
}

void ChatTurn::cancel( const std::string &why )
{
    if( !busy() )
        return;
    // The requests are interruptible; a command that is already running is not, so
    // cancelling mid-tool stops the turn once that tool has reported.
    if( state_ == tsAsking )
        ask_.abort( why );
    else if( state_ == tsJudging )
        judge_.abort( why );
    else
        finishTurn( false );
}

void ChatTurn::onText( const std::string &text, bool thinking )
{
    // Only the conversation's model speaks to the transcript.  The permission agent's
    // description is read by `decide()`, never shown as though it were the answer.
    if( state_ != tsAsking )
        return;
    if( sink_ )
        sink_->onText( text, thinking );
}

void ChatTurn::onFinished( bool ok, const std::string &error )
{
    if( state_ == tsJudging )
    {
        afterJudge( ok );
        return;
    }
    if( state_ == tsAsking )
        afterAnswer( ok, error );
}

bool ChatTurn::sendRound()
{
    OaiRequest req;
    req.model = model_;
    req.temperature = temperature_;
    req.stream = true;
    // The host brief rides every request as the system message: the
    // model must know it is driving OS/2 (or whatever this is) before
    // it writes its first command line, because its training data
    // says otherwise. Prepped here rather than stored in the history
    // so a long conversation never carries a stale copy of a fact
    // that belongs to the binary, not the chat.
    const std::string systemText = atui::core::buildSystemMessage();
    if( !systemText.empty() )
        req.messages.push_back( OaiMessage( "system", systemText ) );
    req.messages.insert( req.messages.end(), msgs_.begin(), msgs_.end() );
    req.toolsJson = toolsJson_;

    state_ = tsAsking;
    std::string error;
    if( !ask_.begin( baseUrl_, apiKey_, req, this, connectSeconds_, idleSeconds_,
                     &error ) )
    {
        if( sink_ )
            sink_->onStatus( "request failed: " + error );
        finishTurn( false );
        return false;
    }
    return true;
}

void ChatTurn::afterAnswer( bool ok, const std::string &error )
{
    if( !ok )
    {
        // Say why.  A silent stop reads as the program hanging up, and a timeout in
        // particular is something the user can raise.
        if( sink_ )
            sink_->onStatus( "request failed: " + error );
        finishTurn( false );
        return;
    }

    const OaiResponse resp = ask_.response();

    OaiMessage asst;
    asst.role = "assistant";
    asst.content = resp.content;
    asst.toolCalls = resp.toolCalls;
    if( thinkingResend_ )
        asst.reasoning = resp.reasoning;
    msgs_.push_back( asst );

    if( !( resp.finishReason == "tool_calls" && !resp.toolCalls.empty() ) )
    {
        conv_ = msgs_;           // the model has seen all of this now
        finishTurn( true );
        return;
    }

    pendingTools_ = resp.toolCalls;
    toolIndex_ = 0;
    decideCurrentCall();
}

void ChatTurn::decideCurrentCall()
{
    skipRun_ = false;
    pendingResult_.clear();
    pendingNote_.clear();
    answered_ = false;

    if( toolIndex_ >= pendingTools_.size() )
    {
        // Every call in this round has been answered: ask the model again so it can
        // turn the results into an answer.
        if( round_ >= kMaxToolRounds )
        {
            if( sink_ )
                sink_->onStatus( "the model asked for tools "
                                 + core::toString( round_ )
                                 + " times without answering; stopping here" );
            conv_ = msgs_;
            finishTurn( true );
            return;
        }
        ++round_;
        sendRound();
        return;
    }

    const OaiToolCall &tc = pendingTools_[toolIndex_];
    callName_ = tc.name;
    callArgs_ = tc.arguments;

    atui::tools::Tool *tool =
        atui::tools::ToolRegistry::instance().find( callName_ );

    // Recorded the moment it is asked for, so what was done stays visible afterwards
    // and an unknown tool is at least recorded with its raw arguments.
    if( sink_ )
    {
        const std::string preview =
            tool ? tool->callPreview( callArgs_ )
                 : callName_ + " " + atui::tools::previewClip( callArgs_, 200 );
        sink_->onToolCall( preview );
    }

    if( !tool )
    {
        skipRun_ = true;
        pendingResult_ = "error: unknown tool '" + callName_ + "'";
        runOrSkip();
        return;
    }

    if( !tool->requiresApproval() )
    {
        runOrSkip();
        return;
    }

    // An earlier "allow this session" for the identical call outranks the policy, and
    // is the only thing that does.  It is still logged: half of what the program did
    // would otherwise leave no record.
    if( atui::tools::isAllowed( callName_, callArgs_ ) )
    {
        pendingNote_ = "allowed for this session";
        atui::tools::Permission remembered;
        remembered.outcome = atui::tools::poRun;
        remembered.rung = atui::tools::prSessionAllowed;
        remembered.reason = pendingNote_;
        atui::tools::logDecision( callName_, remembered, std::string(), 0,
                                  tool->callPreview( callArgs_ ) );
        runOrSkip();
        return;
    }

    const atui::tools::Permission facts =
        atui::tools::decide( tool->permissionSubject( callArgs_ ) );
    if( facts.outcome != atui::tools::poAgent )
    {
        pendingNote_ = atui::tools::decisionNote( facts );
        atui::tools::logDecision( callName_, facts, permissionAgentModel(), 0,
                                  tool->callPreview( callArgs_ ) );
        if( facts.outcome == atui::tools::poRun )
            runOrSkip();
        else
            askUser();
        return;
    }

    // The facts do not settle it, so ask what the call would do.  Cached first: a
    // repeated command should not cost a second model call, and the cache is the same
    // one the blocking path uses, so the two cannot disagree about what has already
    // been described.
    const std::string agent = permissionAgentModel();
    judgeKey_ = atui::tools::planCacheKey( callName_, callArgs_, agent,
                                           atui::tools::projectRoot() );
    atui::tools::EffectPlan plan;
    if( atui::tools::cachedPlan( judgeKey_, plan ) )
    {
        const atui::tools::Permission p =
            atui::tools::decideWithPlan( tool->permissionSubject( callArgs_ ),
                                         &plan, true );
        pendingNote_ = atui::tools::decisionNote( p );
        atui::tools::logDecision( callName_, p, agent, 0,
                                  tool->callPreview( callArgs_ ) );
        if( p.outcome == atui::tools::poRun )
            runOrSkip();
        else
            askUser();
        return;
    }

    OaiRequest question;
    std::string unused;
    if( !atui::tools::permissionJudge() ||
        !buildJudgeRequest( callName_, callArgs_, question ) ||
        !judge_.begin( baseUrl_, apiKey_, question, this, connectSeconds_,
                       idleSeconds_, &unused ) )
    {
        // "Ask the agent" with nobody to ask is a question for the user, and the
        // reason says so: a permission system that fails silently is one nobody can
        // trust two minutes later.
        pendingNote_ = "no permission agent is available to check it";
        askUser();
        return;
    }
    state_ = tsJudging;
}

void ChatTurn::afterJudge( bool ok )
{
    atui::tools::Tool *tool =
        atui::tools::ToolRegistry::instance().find( callName_ );

    if( !ok )
    {
        pendingNote_ = "the permission agent could not describe it";
        askUser();
        return;
    }

    const OaiResponse answer = judge_.response();
    atui::tools::EffectPlan plan;
    parseJudgeReply( callName_, answer.content, answer.reasoning, plan );
    if( plan.parsed )
        atui::tools::rememberPlan( judgeKey_, plan );

    const atui::tools::Permission p = atui::tools::decideWithPlan(
        tool ? tool->permissionSubject( callArgs_ )
             : atui::tools::PermissionSubject(),
        plan.parsed ? &plan : 0, false );
    pendingNote_ = atui::tools::decisionNote( p );
    atui::tools::logDecision( callName_, p, permissionAgentModel(), 0,
                              tool ? tool->callPreview( callArgs_ ) : callName_ );

    if( p.outcome == atui::tools::poRun )
        runOrSkip();
    else
        askUser();
}

void ChatTurn::askUser()
{
    atui::tools::Tool *tool =
        atui::tools::ToolRegistry::instance().find( callName_ );

    question_ = atui::tools::ApprovalRef( new atui::tools::Approval() );
    question_->tool = callName_;
    question_->summary = tool ? tool->approvalSummary( callArgs_ ) : callArgs_;
    question_->reason = pendingNote_;
    answered_ = false;
    allow_ = false;
    allowSession_ = false;
    state_ = tsAwaitingApproval;

    if( sink_ )
        sink_->onApprovalNeeded( question_ );
    else
    {
        // No UI to ask is a refusal, and the model is told which.
        skipRun_ = true;
        pendingResult_ = "error: tool '" + callName_ + "' requires approval but no "
                         "approval UI is available";
        pendingNote_ += " (no approval UI to ask)";
        runOrSkip();
    }
}

void ChatTurn::runOrSkip()
{
    // Not executed here: the step boundary is what lets the screen show the call, and
    // whoever is drawing catch up, before a child process takes the loop.
    state_ = tsRunningTool;
}

void ChatTurn::finishCall( const std::string &result )
{
    if( sink_ )
        sink_->onToolResult( result );

    OaiMessage tr;
    tr.role = "tool";
    tr.toolCallId = pendingTools_[toolIndex_].id;
    tr.name = callName_;
    tr.content = result;
    msgs_.push_back( tr );

    ++toolIndex_;
    decideCurrentCall();
}

void ChatTurn::finishTurn( bool ok )
{
    if( state_ == tsDone )
        return;
    state_ = tsDone;
    question_ = atui::tools::ApprovalRef();
    if( sink_ )
        sink_->onTurnFinished( ok );
}

} // namespace atui::net
} // namespace atui
