/* The chat turn from the shell: the same machine, the same code, no terminal.
 *
 * Why this exists: `atui.exe` in a telnet window reports a failed chat turn
 * ("the connection could not read: Connection reset by peer") and the only way to
 * investigate through that window is to type at it -- and typing over this bridge
 * loses characters, so an experiment cannot be trusted to have asked the question it
 * meant to ask.  This asks the same question without a terminal in sight: it runs
 * `net::ChatTurn`, the very object the interface drives, and prints every record the
 * screen would have shown.  A failure here is the application's; a failure that only
 * appears in the TUI is the terminal's.
 *
 * Linked against the objects `build.cmd atui` produced (everything but main.obj), so
 * there is no second copy of the request builder that could behave differently: same
 * settings snapshot, same system prompt, same tools JSON, same transport.
 *
 *   build.cmd atui
 *   build.cmd netrun probe_turn.cpp pturn.exe
 *   set ATUI_BASE_URL=http://host:10001/v1& set ATUI_MODEL=name& pturn.exe "say hi"
 *
 * It never runs a tool: an approval question is answered "no" here, because a probe
 * that executes commands is not a probe.  ATUI_HTTP_DEBUG=1 puts the outgoing request
 * on stderr, so `pturn.exe "..." 2>req.dbg` leaves the bytes to be looked at, which is
 * how the reset was chased.
 */
#include "core/paths.h"          // setArgv0: where the undo journal and "home" come from
#include "net/chat_turn.h"
#include "net/http_simple.h"
#include "net/oai_body.h"
#include "net/permission_judge.h"
#include "net/provider.h"
#include "settings/settings.h"
#include "tools/permissions.h"
#include "tools/tool_registry.h"   // registerDefaults, ToolRegistry::renderToolsJson

#include <stdlib.h>             // atoi
#include <stdio.h>
#include <string.h>

/* DosSleep, declared rather than included: <os2.h> wants the include order the socket
 * headers need and this probe wants none of it. The signature is the Toolkit's
 * (h/bsedos.h: `APIRET APIENTRY DosSleep( ULONG msec )`), and APIENTRY is Watcom's
 * _System for the OS/2 32-bit API. os2386.lib is already on the link line. */
extern "C" unsigned long _System DosSleep( unsigned long msec );

namespace {

/* Says what the machine decided, in order, and counts what it saw: the counts are
 * what a run is judged on, the lines are for the person reading the log. */
class Printer : public atui::net::ChatTurn::Sink
{
public:
    Printer() : texts_( 0 ), thinks_( 0 ), statuses_( 0 ), calls_( 0 )
              , notes_( 0 ), results_( 0 ), approvals_( 0 ), finished_( 0 ), ok_( false )
              , sinkDelayMs_( 0 )
    {
    }

    /* Stand-in for a screen. The interface redraws for every piece of the answer, and
     * a redraw goes out over the same telnet link the answer arrives on, so the read
     * stops while the write happens. `--sinkdelay N` makes each callback take N ms and
     * asks whether the server tolerates a client that stops reading for a second at a
     * time -- which is the same question, asked without a terminal in the way. */
    void setSinkDelay( int ms ) { sinkDelayMs_ = ms; }

    virtual void onText( const std::string &text, bool thinking )
    {
        if( sinkDelayMs_ > 0 )
            DosSleep( (unsigned long) sinkDelayMs_ );
        if( thinking ) ++thinks_;
        else         ++texts_;
        const int shown = thinking ? thinks_ : texts_;
        if( shown <= 4 )
            printf( "  %s  %.100s\n", thinking ? "think:" : "text: ", text.c_str() );
    }

    virtual void onStatus( const std::string &text )
    {
        ++statuses_;
        printf( "  status: %s\n", text.c_str() );
    }

    virtual void onToolCall( const std::string &preview )
    {
        ++calls_;
        printf( "  tool call: %.160s\n", preview.c_str() );
    }

    virtual void onToolNote( const std::string &note )
    {
        ++notes_;
        printf( "  tool note: %.160s\n", note.c_str() );
    }

    virtual void onToolResult( const std::string &result )
    {
        ++results_;
        printf( "  tool result: %.160s\n", result.c_str() );
    }

    virtual void onApprovalNeeded( const atui::tools::ApprovalRef &question )
    {
        ++approvals_;
        printf( "  approval asked: %.160s -- answering no (a probe runs nothing)\n",
                question->summary.c_str() );
    }

    virtual void onTurnFinished( bool ok )
    {
        ++finished_;
        ok_ = ok;
        printf( "  finished: ok=%d\n", (int) ok );
    }

    void report() const
    {
        printf( "counts: text=%d thinking=%d status=%d tool_calls=%d notes=%d "
                "results=%d approvals=%d finished=%d ok=%d\n",
                texts_, thinks_, statuses_, calls_, notes_, results_, approvals_,
                finished_, (int) ok_ );
    }

private:
    int texts_, thinks_, statuses_, calls_, notes_, results_, approvals_, finished_;
    int sinkDelayMs_;
    bool ok_;
};

/* What the turn is about to ask for, on one line each: the endpoint as parsed, and
 * the two things the permission agent's request does not carry -- the tool list and
 * the temperature -- because that is where the chat request and the judge request
 * differ, and a server that resets one and not the other is answering something. */
void reportConfig()
{
    const atui::net::ProviderSettings cfg = atui::net::providerSettings();
    printf( "config: base=%s model=%s temperature=%g thinkingResend=%d models=%d\n",
            cfg.baseUrl.c_str(), cfg.model.c_str(), cfg.temperature,
            (int) cfg.thinkingResend, (int) cfg.models.size() );

    atui::tools::registerDefaults();
    const std::string tools = atui::tools::ToolRegistry::instance().renderToolsJson();
    printf( "tools json: %d bytes, first 120: [%.120s]\n", (int) tools.size(),
            tools.c_str() );

    /* Written out as well: `get ptools.json` and `python3 -m json.tool` says whether
     * this target built valid JSON, which is a question the C++ cannot answer by
     * looking at itself. */
    FILE *f = std::fopen( "ptools.json", "wb" );
    if( f )
    {
        std::fwrite( tools.data(), 1, tools.size(), f );
        std::fclose( f );
        printf( "tools json written to ptools.json\n" );
    }
    else
        printf( "could not write ptools.json\n" );
}

} // anonymous namespace

int main( int argc, char **argv )
{
    atui::core::setArgv0( argc > 0 ? argv[ 0 ] : 0 );
    atui::settings::loadAll();
    atui::tools::setPermissionJudge( &atui::net::oaiPermissionJudge() );

    std::string message = "say hello in exactly three words";
    for( int i = 1; i < argc; ++i )
        if( argv[ i ][ 0 ] != '-' ) message = argv[ i ];

    reportConfig();
    fflush( stdout );

    Printer printer;
    atui::net::ChatTurn turn;

    if( !turn.start( message, &printer ) )
    {
        printf( "could not start the turn at all\n" );
        return 2;
    }
    printf( "turn started, pumping\n" );
    fflush( stdout );

    /* How to pump, because "who steps it" is the one thing the state machine is
     * supposed to make irrelevant -- and a connection that survives `pumpUntilDone`
     * and resets in the TUI says it is not irrelevant yet.  `--step N --gap M` steps
     * with N ms of wait and M ms of delay between steps, which is what the interface
     * does (it has its own drawing to fit in between), while the default is the tight
     * pump a CLI caller would use.
     *
     * Generous budget either way: on this box a 27B model takes its time, and a probe
     * that gives up early reports a failure the application would not have had. */
    int stepMs = 25, gapMs = -1;
    for( int i = 1; i < argc; ++i )
    {
        if( strcmp( argv[ i ], "--step" ) == 0 && i + 1 < argc ) stepMs = atoi( argv[ ++i ] );
        else if( strcmp( argv[ i ], "--gap" ) == 0 && i + 1 < argc ) gapMs = atoi( argv[ ++i ] );
    }

    int sinkDelay = 0;
    for( int i = 1; i < argc; ++i )
        if( strcmp( argv[ i ], "--sinkdelay" ) == 0 && i + 1 < argc )
            sinkDelay = atoi( argv[ ++i ] );
    printer.setSinkDelay( sinkDelay );
    if( sinkDelay > 0 )
        printf( "sink: every callback sleeps %d ms\n", sinkDelay );
    fflush( stdout );

    bool done = false;
    if( gapMs < 0 )
    {
        done = atui::net::pumpUntilDone( turn, 300 );
    }
    else
    {
        printf( "pumping: step(%d) every %d ms\n", stepMs, gapMs );
        fflush( stdout );
        long waited = 0;
        while( turn.busy() && waited < 300000L )
        {
            turn.step( stepMs );
            DosSleep( (unsigned long) gapMs );   // yield, as tvision does between ticks
            waited += stepMs + gapMs;
        }
        done = !turn.busy();
    }

    printf( "pump: %s\n", done ? "finished" : "timed out, still busy" );
    if( !done )
        turn.cancel( "probe is giving up" );
    printer.report();
    fflush( stdout );
    return done ? 0 : 1;
}
