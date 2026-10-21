// Tests for the chat turn: the agent loop -- model, tools, the policy, the judging
// agent, the user -- run as a state machine against the scripted responder.
//
// These are the tests that say the loop is the same loop the threaded model used to
// run: an answer is committed and carried, a tool call is recorded before it runs and
// reported after, a call the policy cannot clear ends up as a question and a "no" is
// not a maybe, "allow this session" is exact to the arguments, a failed request
// leaves the history alone, and a model that only ever asks for tools is stopped out
// loud.  None of it runs in a thread, which is the point: the same tests are what the
// Open Watcom build will run, on a platform with no threads to get wrong.
//
// Network-only: nothing here is compiled when ATUI_HAVE_NETWORK is off.
#include "harness.h"

#ifdef ATUI_HAVE_NETWORK

#include "http_pipe.h"
#include "sse_script.h"

#include "net/chat_turn.h"
#include "net/provider.h"

#include "tools/judge.h"
#include "tools/permissions.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace atui_test {

namespace {

using atui::net::ChatTurn;

// --- the sink -------------------------------------------------------------------

// Records everything the turn says, in one string per kind plus a count, so a test
// can assert both what was said and how many times it was said -- "the user was asked
// exactly once" is the interesting half of an approval test.
struct Recorder : public ChatTurn::Sink
{
    std::string text;
    std::string thinking;
    std::string status;
    std::string toolCalls;
    std::string notes;
    std::string results;
    std::vector<std::string> questions;
    std::string reasons;
    int finishes;
    bool lastOk;

    Recorder() : finishes( 0 ), lastOk( false ) {}

    virtual void onText( const std::string &piece, bool isThinking )
    {
        if( isThinking ) thinking += piece;
        else             text += piece;
    }
    virtual void onStatus( const std::string &line )
    {
        status += line;
        status += "\n";
    }
    virtual void onToolCall( const std::string &preview )
    {
        toolCalls += preview;
        toolCalls += "\n";
    }
    virtual void onToolNote( const std::string &note )
    {
        notes += note;
        notes += "\n";
    }
    virtual void onToolResult( const std::string &result )
    {
        results += result;
        results += "\n";
    }
    virtual void onApprovalNeeded( const atui::tools::ApprovalRef &question )
    {
        questions.push_back( question ? question->summary : std::string() );
        reasons += question ? question->reason : std::string();
        reasons += "\n";
    }
    virtual void onTurnFinished( bool ok )
    {
        ++finishes;
        lastOk = ok;
    }

    static int countIn( const std::string &haystack, const std::string &needle )
    {
        int n = 0;
        for( std::size_t at = haystack.find( needle ); at != std::string::npos;
             at = haystack.find( needle, at + 1 ) )
            ++n;
        return n;
    }
};

// Steps the turn until it ends, answering each approval question with `allow`
// (and `session` for whether that answer covers the identical call again).
// Returns non-zero if the turn was still running when the clock ran out.
int driveTurn( ChatTurn &turn, Recorder &rec, bool allow = false,
               bool session = false, int limitMs = 15000 )
{
    const std::chrono::steady_clock::time_point end =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( limitMs );
    std::size_t answered = 0;
    while( turn.busy() && std::chrono::steady_clock::now() < end )
    {
        turn.step( 5 );
        while( answered < rec.questions.size() )
        {
            turn.answerApproval( allow, session );
            ++answered;
        }
    }
    return turn.busy() ? -1 : 0;
}

// --- the surroundings -----------------------------------------------------------

// Points the process-wide provider at `server`, and puts back everything a turn can
// change: the endpoint, the permission mode and root, the judge, the plan cache and
// the session's allowances.  The caches in particular have to start empty, or one
// test's cleared command silently answers another test's question.
class TurnFixture
{
public:
    bool ok;
    std::string base;
    std::string root;

    TurnFixture()
        : ok( false )
        , savedMode_( atui::tools::permissionMode() )
        , savedRoot_( atui::tools::projectRoot() )
        , savedProvider_( atui::net::providerSettings() )
    {
        const char *tmp = std::getenv( "TMPDIR" );
        if( !tmp || !*tmp ) tmp = "/tmp";
        base = std::string( tmp );
        while( base.size() > 1 && base[ base.size() - 1 ] == '/' )
            base.erase( base.size() - 1 );
        base += "/atui_turn";
        ::mkdir( base.c_str(), 0755 );          // may already exist
        root = base + "/proj";
        ::mkdir( root.c_str(), 0755 );

        atui::tools::setProjectRoot( root );
        root = atui::tools::projectRoot();
        atui::tools::setPermissionJudge( 0 );
        atui::tools::clearPlanCache();
        atui::tools::clearAllowed();
        ok = !root.empty();
    }

    ~TurnFixture()
    {
        atui::net::providerSettings() = savedProvider_;
        atui::tools::setPermissionJudge( 0 );
        atui::tools::permissionMode() = savedMode_;
        atui::tools::setProjectRoot( savedRoot_ );
        atui::tools::clearPlanCache();
        atui::tools::clearAllowed();
    }

    // A file in the scratch base -- deliberately outside the project root, so a
    // command that touches it is reaching out of the tree and has to be asked about.
    std::string marker( const char *name ) const { return base + "/" + name; }

private:
    TurnFixture( const TurnFixture & );
    TurnFixture &operator=( const TurnFixture & );

    atui::tools::PermissionMode savedMode_;
    std::string savedRoot_;
    atui::net::ProviderSettings savedProvider_;
};

void useServer( int port )
{
    atui::net::ProviderSettings &p = atui::net::providerSettings();
    p.baseUrl = "http://127.0.0.1:" + std::to_string( port ) + "/v1";
    p.apiKey = "";
    p.model = "test-model";
    p.temperature = 0.0;
}

bool exists( const std::string &path )
{
    struct stat st;
    return ::stat( path.c_str(), &st ) == 0;
}

// A `run_command` call running `command` if it is allowed to.
std::string commandCall( const std::string &id, const std::string &command )
{
    return atui_test::sseToolCall( id, "run_command",
                                   "{\"command\":\"" + atui_test::esc( command ) + "\"}" );
}

// ... and one that would create `path` if it ran.
std::string touchCall( const std::string &id, const std::string &path )
{
    return commandCall( id, "touch '" + path + "'" );
}

// Something to install as the permission agent: ChatTurn only asks whether one
// exists, because it sends the judge's question itself, so this is a placeholder for
// "there is somebody to ask" and its own `plan()` is never reached.
class PresentJudge : public atui::tools::PermissionJudge
{
public:
    virtual void plan( const std::string &, const std::string &,
                       atui::tools::EffectPlan &out )
    {
        out = atui::tools::EffectPlan();
    }
};

// --- the tests ------------------------------------------------------------------

int test_turn_answers_and_carries()
{
    TurnFixture f;
    if( !f.ok ) return -1;
    atui_test::ScriptedHttp server;
    if( !server.start() ) return -2;
    useServer( server.port() );
    atui::tools::permissionMode() = atui::tools::pmAskAll;

    server.enqueueResponse( sseResponse( sseAnswer( "first reply" ) ) );

    Recorder one;
    ChatTurn turn;
    if( !turn.start( "hello there", &one ) ) return -3;
    if( driveTurn( turn, one ) != 0 ) return -4;
    if( one.text != "first reply" ) return -5;
    if( one.finishes != 1 || !one.lastOk ) return -6;
    if( turn.messageCount() != 2 ) return -7;         // the user and the answer

    // The next turn is asked about both of them: a history the model never saw is
    // worse than no history, because it reads as the model having forgotten.
    server.enqueueResponse( sseResponse( sseAnswer( "second" ) ) );
    Recorder two;
    if( !turn.start( "and now?", &two ) ) return -8;
    if( driveTurn( turn, two ) != 0 ) return -9;
    if( two.text != "second" ) return -10;
    if( turn.messageCount() != 4 ) return -11;

    const std::string wire = server.lastRequest();
    if( wire.find( "hello there" ) == std::string::npos ) return -12;
    if( wire.find( "first reply" ) == std::string::npos ) return -13;
    if( wire.find( "and now?" ) == std::string::npos ) return -14;
    return 0;
}

// A tool that needs no approval: recorded, run, and its result handed back to the
// model, which then answers.
int test_turn_runs_a_tool_and_answers_with_it()
{
    TurnFixture f;
    if( !f.ok ) return -1;
    atui_test::ScriptedHttp server;
    if( !server.start() ) return -2;
    useServer( server.port() );
    atui::tools::permissionMode() = atui::tools::pmAskAll;

    server.enqueueResponse( sseResponse( sseToolCall( "call_1", "get_time", "{}" ) ) );
    server.enqueueResponse( sseResponse( sseAnswer( "it is late" ) ) );

    Recorder rec;
    ChatTurn turn;
    if( !turn.start( "what time is it", &rec ) ) return -3;
    if( driveTurn( turn, rec ) != 0 ) return -4;

    if( rec.toolCalls.empty() ) return -5;
    if( rec.results.empty() ) return -6;
    if( !rec.questions.empty() ) return -7;           // nothing to approve
    if( rec.text != "it is late" ) return -8;
    if( !rec.lastOk ) return -9;

    const std::string wire = server.lastRequest();
    if( wire.find( "\"role\":\"tool\"" ) == std::string::npos ) return -10;
    if( wire.find( "call_1" ) == std::string::npos ) return -11;
    return 0;
}

// The property the whole permission system is here for: a call the user says no to
// does not happen, is reported as declined, and the model is told.
int test_turn_declines_without_running()
{
    TurnFixture f;
    if( !f.ok ) return -1;
    atui_test::ScriptedHttp server;
    if( !server.start() ) return -2;
    useServer( server.port() );
    atui::tools::permissionMode() = atui::tools::pmAskAll;

    const std::string ran = f.marker( "ran_declined.txt" );
    ::remove( ran.c_str() );

    server.enqueueResponse( sseResponse( touchCall( "c1", ran ) ) );
    server.enqueueResponse( sseResponse( sseAnswer( "understood" ) ) );

    Recorder rec;
    ChatTurn turn;
    if( !turn.start( "make a file", &rec ) ) return -3;
    if( driveTurn( turn, rec, false, false ) != 0 ) return -4;

    if( rec.questions.size() != 1 ) return -5;
    if( exists( ran ) ) return -6;                    // it did not run
    if( rec.results.find( "declined" ) == std::string::npos ) return -7;
    if( rec.notes.find( "(you declined)" ) == std::string::npos ) return -8;
    if( rec.text != "understood" ) return -9;         // the model got its turn anyway
    if( server.lastRequest().find( "declined" ) == std::string::npos ) return -10;
    return 0;
}

int test_turn_runs_what_was_allowed()
{
    TurnFixture f;
    if( !f.ok ) return -1;
    atui_test::ScriptedHttp server;
    if( !server.start() ) return -2;
    useServer( server.port() );
    atui::tools::permissionMode() = atui::tools::pmAskAll;

    const std::string ran = f.marker( "ran_allowed.txt" );
    ::remove( ran.c_str() );

    server.enqueueResponse( sseResponse( touchCall( "c1", ran ) ) );
    server.enqueueResponse( sseResponse( sseAnswer( "done" ) ) );

    Recorder rec;
    ChatTurn turn;
    if( !turn.start( "make a file", &rec ) ) return -3;
    if( driveTurn( turn, rec, true, false ) != 0 ) return -4;

    if( rec.questions.size() != 1 ) return -5;
    if( !exists( ran ) ) return -6;                   // allowed, so it ran
    if( rec.notes.find( "(you allowed it)" ) == std::string::npos ) return -7;
    ::remove( ran.c_str() );
    return 0;
}

// "Allow this session" is a decision about this command, not a door left open: the
// identical call goes through without asking again, and a different one is still a
// question.
int test_turn_session_allowance_is_exact()
{
    TurnFixture f;
    if( !f.ok ) return -1;
    atui_test::ScriptedHttp server;
    if( !server.start() ) return -2;
    useServer( server.port() );
    atui::tools::permissionMode() = atui::tools::pmAskAll;

    const std::string a = f.marker( "session_a.txt" );
    const std::string b = f.marker( "session_b.txt" );
    ::remove( a.c_str() );
    ::remove( b.c_str() );

    server.enqueueResponse( sseResponse( touchCall( "c1", a ) ) );   // asked, allowed
    server.enqueueResponse( sseResponse( touchCall( "c2", a ) ) );   // same command
    server.enqueueResponse( sseResponse( touchCall( "c3", b ) ) );   // different one
    server.enqueueResponse( sseResponse( sseAnswer( "finished" ) ) );

    Recorder rec;
    ChatTurn turn;
    if( !turn.start( "make two files", &rec ) ) return -3;
    if( driveTurn( turn, rec, true, true ) != 0 ) return -4;

    if( !exists( a ) || !exists( b ) ) return -5;
    if( rec.questions.size() != 2 ) return -6;        // the second call asked nobody
    if( Recorder::countIn( rec.notes, "allowed for this session" ) != 1 ) return -7;
    if( rec.text != "finished" ) return -8;
    ::remove( a.c_str() );
    ::remove( b.c_str() );
    return 0;
}

// A turn that failed has not happened: the stored history is what the model last saw,
// so the next message is not asked against an exchange that never arrived.
int test_turn_failure_leaves_history_alone()
{
    TurnFixture f;
    if( !f.ok ) return -1;
    atui_test::ScriptedHttp server;
    if( !server.start() ) return -2;
    useServer( server.port() );
    atui::tools::permissionMode() = atui::tools::pmAskAll;

    server.enqueueResponse( errorResponse( "500 Internal Server Error",
                                           "{\"error\":\"boom\"}" ) );

    Recorder rec;
    ChatTurn turn;
    if( !turn.start( "hello", &rec ) ) return -3;
    if( driveTurn( turn, rec ) != 0 ) return -4;
    if( rec.finishes != 1 || rec.lastOk ) return -5;
    if( rec.status.find( "500" ) == std::string::npos ) return -6;
    if( turn.messageCount() != 0 ) return -7;
    return 0;
}

// Two messages at once is a thing users do, and the second one is refused rather than
// started alongside: interleaved turns mean interleaved history.
int test_turn_refuses_a_second_message()
{
    TurnFixture f;
    if( !f.ok ) return -1;
    atui_test::ScriptedHttp server;
    if( !server.start() ) return -2;
    useServer( server.port() );
    atui::tools::permissionMode() = atui::tools::pmAskAll;

    // Split in half with a pause between, so the turn is definitely still running
    // when the second message arrives.
    const std::string response = sseResponse( sseAnswer( "eventually" ) );
    server.enqueueResponse( response, 400, response.size() / 2 );

    Recorder first;
    ChatTurn turn;
    if( !turn.start( "one", &first ) ) return -3;
    Recorder second;
    if( turn.start( "two", &second ) ) return -4;     // refused, not queued
    if( driveTurn( turn, first ) != 0 ) return -5;
    if( first.text != "eventually" ) return -6;
    if( second.finishes != 0 ) return -7;             // nothing was ever started
    return 0;
}

// The judging agent runs over the same transport, and an answer that could not be
// read is not a permission: the user is asked, and nothing has run.  This is the
// event-loop version of the property the blocking path has always had.
int test_turn_asks_when_the_judgement_is_unreadable()
{
    TurnFixture f;
    if( !f.ok ) return -1;
    atui_test::ScriptedHttp server;
    if( !server.start() ) return -2;
    useServer( server.port() );
    atui::tools::permissionMode() = atui::tools::pmAgent;
    PresentJudge judge;
    atui::tools::setPermissionJudge( &judge );

    // A command whose path the policy can read is settled by the facts -- reaching
    // outside the tree is rung 7 and never needs an agent -- so this one hides its
    // write behind a `cd`, which is precisely the kind of thing the facts miss and
    // the agent exists to describe.
    const std::string ran = f.root + "/ran_judged.txt";
    ::remove( ran.c_str() );

    server.enqueueResponse(
        sseResponse( commandCall( "c1", "cd '" + f.root + "' && touch ran_judged.txt" ) ) );
    // The agent's turn: prose, and no effects listed, so nothing was described.
    server.enqueueResponse(
        sseResponse( sseAnswer( "this command seems harmless to me" ) ) );
    server.enqueueResponse( sseResponse( sseAnswer( "as you decide" ) ) );

    Recorder rec;
    ChatTurn turn;
    if( !turn.start( "make a file", &rec ) ) return -3;
    if( driveTurn( turn, rec, false, false ) != 0 ) return -4;

    if( exists( ran ) ) return -5;
    if( rec.questions.size() != 1 ) return -6;
    // It asked the agent and got nothing usable -- which is different from never
    // having an agent at all, and the note the user is shown says which.
    if( rec.reasons.find( "no permission agent" ) != std::string::npos ) return -7;
    if( rec.text != "as you decide" ) return -8;
    return 0;
}

// The loop guard: a model that only ever asks for tools is stopped at the limit, and
// says so.  Seven answers are scripted and only six are used -- if the guard did not
// exist, the seventh request would go out and the turn would not end.
int test_turn_stops_at_the_tool_round_limit()
{
    TurnFixture f;
    if( !f.ok ) return -1;
    atui_test::ScriptedHttp server;
    if( !server.start() ) return -2;
    useServer( server.port() );
    atui::tools::permissionMode() = atui::tools::pmAskAll;

    for( int i = 0; i < 7; ++i )
    {
        const std::string id = "c" + std::to_string( i );
        server.enqueueResponse( sseResponse( sseToolCall( id, "get_time", "{}" ) ) );
    }

    Recorder rec;
    ChatTurn turn;
    if( !turn.start( "the time, forever", &rec ) ) return -3;
    if( driveTurn( turn, rec, false, false, 20000 ) != 0 ) return -4;

    if( rec.status.find( "stopping here" ) == std::string::npos ) return -5;
    if( rec.finishes != 1 ) return -6;
    if( Recorder::countIn( rec.toolCalls, "\n" ) != 6 ) return -7;
    return 0;
}

} // anonymous namespace

void init_chat_turn_tests()
{
    register_test( "turn: answers and carries the history",
                   &test_turn_answers_and_carries );
    register_test( "turn: runs a tool and answers with it",
                   &test_turn_runs_a_tool_and_answers_with_it );
    register_test( "turn: a declined call does not happen",
                   &test_turn_declines_without_running );
    register_test( "turn: an allowed call happens",
                   &test_turn_runs_what_was_allowed );
    register_test( "turn: a session allowance is exact to the arguments",
                   &test_turn_session_allowance_is_exact );
    register_test( "turn: a failed request leaves the history alone",
                   &test_turn_failure_leaves_history_alone );
    register_test( "turn: a second message mid-turn is refused",
                   &test_turn_refuses_a_second_message );
    register_test( "turn: an unreadable judgement asks instead of running",
                   &test_turn_asks_when_the_judgement_is_unreadable );
    register_test( "turn: the tool round limit stops and says so",
                   &test_turn_stops_at_the_tool_round_limit );
}

} // namespace atui_test

#endif // ATUI_HAVE_NETWORK
