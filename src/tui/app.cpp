// Bare atui TUI shell (M0/M1/M2): menu bar, status line, About dialog, exit,
// and an AI chat window (transcript + input line).
//
// TVision classes live in the global namespace; atui code lives in atui::tui.
// The Uses_* macros must precede the tv.h include; they gate which TVision
// classes are pulled in.
//
// The init* functions are static (invoked by TProgInit during TVision init),
// so the chat widgets live in module-level state and are owned by the single
// TApp instance.
#define Uses_TKeys
#define Uses_TApplication
#define Uses_TProgram
#define Uses_TEvent
#define Uses_THardwareInfo       // only for getTickCountMs(), in the debug log below
#define Uses_TRect
#define Uses_TView
#define Uses_TGroup
#define Uses_TMenuBar
#define Uses_TSubMenu
#define Uses_TMenuItem
#define Uses_TMenu
#define Uses_TLabel
#define Uses_TStatusLine
#define Uses_TStatusDef
#define Uses_TStatusItem
#define Uses_TDialog
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TRadioButtons
#define Uses_TCheckBoxes
#define Uses_TSItem
#define Uses_TDeskTop
#define Uses_TBackground
#define Uses_TTerminal
#define Uses_TTextDevice
#define Uses_TScroller
#define Uses_TInputLine
#define Uses_TScrollBar
#define Uses_TListBox
#define Uses_TStringCollection
#define Uses_TTimer
#define Uses_TScreen
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <tvision/tv.h>

#include "app.h"
#include "core/ai.h"
#include "tui/ascii_fold.h"
#include "core/journal.h"
#include "core/signals.h"
#include "core/strutil.h"      // Fmt and toString: Watcom has no std::ostringstream
#include "tui/theme.h"
#include "tui/md_style.h"
#include "tui/markdown.h"
#include "tui/thinking.h"
#include "tui/wrap.h"
#include "net/provider.h"
#include "settings/settings.h"
#include "tools/permissions.h"
#include "tools/run_command_tool.h"      // the command timeout field reads it

#ifdef ATUI_HAVE_NETWORK
#include "net/chat_turn.h"
#include "net/turn_queue.h"
#include "core/thread.h"
#include "net/models.h"
#include "net/permission_judge.h"
#include "tools/approval.h"
#endif

#include <vector>

namespace atui {
namespace tui {

using atui::core::String;

// Command ids. cm* are TVision's, Cmd* are ours. The "Switch model" dropdown
// uses a range: the command encodes the index of the picked provider, whose
// configured model comes with it.
enum { CmdHelp = 1000, CmdTheme = 1004, CmdProviders = 1003,
       CmdSettings = 1005, CmdUndo = 1006,
       CmdRefreshModels = 2000,
       CmdSwitchModelBase = 2200 };  // + provider index

// Guard on the size of the generated dropdown (the command range above).
const int kMaxSwitchModels = 40;

class TranscriptView;   // defined below; used via pointer in the globals
class PromptInput;      // defined below; used via pointer in the globals

namespace {
    TranscriptView *g_transcript_ = 0;
    PromptInput *g_prompt_ = 0;
    TScrollBar *g_vBar_ = 0;
    atui::core::ChatModelStub *g_stub_ = 0;

    // `--ask`: the first message, submitted once the prompt exists. Consumed in
    // run_app(), before the loop starts, and then emptied so a restart of the desk
    // cannot ask twice.
    std::string g_startupMessage;
    String g_history_;

    // Desktop rect (saved so the transcript + prompt can reflow together).
    int g_dX1 = 0, g_dY1 = 0, g_dX2 = 0, g_dY2 = 0;

    // Provider/model pickers: the "Switch model" menu-bar dropdown (rebuilt
    // from the provider store) and the status-line item showing the active
    // "provider | model".
    TSubMenu *g_switchModelMenu_ = 0;
    TStatusItem *g_statusProvider_ = 0;
    TStatusItem *g_statusPermission_ = 0;

    // The scripted exit: ATUI_QUIT_AFTER_MS, and the first tick seen, which is what
    // the deadline is measured from (see quitAfterMs_()).  Outside the network block
    // below, because leaving is not a network feature -- the configuration that cannot
    // chat still has to be quittable, and that is what this is for.
    unsigned long g_quitAfterMs_  = 0;
    unsigned long g_firstTickMs_  = 0;

    // M2 streaming state (only used when ATUI_HAVE_NETWORK).
#ifdef ATUI_HAVE_NETWORK
    // One chat turn, the queue it reports through, and -- where the platform has
    // one -- the stack it runs on.
    //
    // The division is deliberate and it is the whole design of this file: the
    // ChatTurn is touched by exactly one thread at a time (the worker when there is
    // one, this one otherwise), and the only thing the UI thread reads is the queue.
    // The approval answer goes back through the queue for the same reason: calling
    // `answerApproval()` from here would be the UI reaching into an object the worker
    // is stepping.
    atui::net::ChatTurn  *g_turn_  = 0;
    atui::net::TurnQueue  g_queue_;

    // The stack a turn runs on while one is running, or nothing.  `running()` is also
    // how "busy" is answered when the user types during a turn.
    atui::core::Thread    g_turnThread_;

    // Set while the timer is pumping -- stepping, draining records into the
    // transcript, or showing the approval dialog, which runs a nested event loop with
    // the 50ms timer still firing underneath it.  Without this, that dialog re-enters
    // the pump that opened it.
    bool g_inStep_ = false;
#endif

    // Debug event log (only when built with ATUI_DEBUG_KEYS).
#ifdef ATUI_DEBUG_KEYS
    std::ofstream g_keylog;
#endif
}

// ATUI_TURN_DEBUG=1 writes one line per interesting event to stderr, with four kinds:
// `ask` (a question was accepted), `drive` (the state machine was stepped, from
// whichever stack owns it in this build), `tick` (tvision delivered a timer event) and
// `pump` (the drain-and-draw pass ran, with the two guards reported as extra numbers
// rather than silently returning early).  It exists because on a target whose terminal
// eats keystrokes the screen cannot be asked the questions that matter when a turn
// appears to hang: *is the pump running at all*, *is the turn advancing*, and -- since
// a worker thread made those two independent -- *is the UI being woken*.  Each
// combination has a different fix, and on the OS/2 box the answers were the whole
// diagnosis: 129 drives in the first eight seconds and no tick for 317, which is a
// waiting UI thread rather than a stalled network.
//
// Written to stderr because that is where `atui.exe 2>turn.dbg` puts it, and the file
// comes off the box over FTP without touching the console.  Fetch it after the process
// exits: HPFS reports a file's last committed size, so a mid-run fetch sees a prefix no
// matter how often the log is flushed.  The timestamp is whole seconds: enough to see a
// rate.
// Milliseconds, from tvision's own clock -- the same one its timer queue reads, so a
// duration measured here can be set against the tick rate without two clocks having to
// agree about anything. Debug use only.
static unsigned long nowMs_()
{
    return (unsigned long) THardwareInfo::getTickCountMs();
}

static void turnDebug_( const char *what, long a, long b )
{
    static const char *on = std::getenv( "ATUI_TURN_DEBUG" );
    if( !on || !*on || *on == '0' )
        return;
    static long n = 0;
    std::fprintf( stderr, "[turn %ld] %s busy=%ld n=%ld (t+%ld)\n",
                  (long) ++n, what, a, b, (long) std::time( 0 ) );
    std::fflush( stderr );
}

// A clock that quits the program, so that leaving atui can be scripted.
//
// OS/2 has no SIGTERM to catch and Alt-X needs a keyboard wired to the machine, so
// before this the only way to exit atui on the target was a person sitting in front of
// it -- which is exactly why its shutdown path (worker joined, terminal restored,
// statics destroyed in some order) had never been run there.  `--ask` made the first
// half of a session scriptable; this makes the second half scriptable, and the pair is
// what lets the whole life of the program be tested from the other side of the world.
//
// It quits through the same endModal Alt-X uses, nothing more.  0 means not armed.
unsigned long quitAfterMs_()
{
    static const char *value = std::getenv( "ATUI_QUIT_AFTER_MS" );
    if( value == 0 || *value == '\0' || *value == '0' )
        return 0;
    unsigned long ms = 0;
    for( const char *p = value; *p >= '0' && *p <= '9'; ++p )
        ms = ms * 10 + (unsigned long) ( *p - '0' );
    return ms;
}

// --- text helpers -------------------------------------------------------------

// Wrapping lives in tui/wrap.{h,cpp}, shared with the reasoning renderer; this
// keeps the transcript's and the approval box's callsites short.
static std::vector<std::string> wrapText_( const std::string &s, int width )
{
    return atui::tui::wrapText( s, width );
}

// Module helpers (outer namespace; definitions appear after TApp).
TStatusLine *initStatusLine_( TRect r );
TMenuBar *initMenuBar_( TRect r );
TDeskTop *initDeskTop_( TRect r );
void appendUser_( const char *text );
void appendReply_( const char *text );
void processMessage_();
void focusInput_();
void relayoutPanels_();
void pumpChat_();
TMenu *buildSwitchModelMenu_();
void refreshSwitchModelMenu_();
void updateProviderStatus_();
void updatePermissionStatus_();
void showProvidersDialog_();
void pickSwitchModel_( int index );
void showUndoDialog_();
void maybeRecoverInterruptedWrites_();

#ifdef ATUI_HAVE_NETWORK
// --- tool approval (the turn asks, this thread answers) ----------------------
//
// A tool the policy could not clear is put to the user: the turn calls
// onApprovalNeeded from inside step(), the sink parks the question here, and the
// next pump shows the modal box and calls answerApproval().  Nothing blocks and
// nothing is handed between threads, so the only rule is that the dialog is shown
// outside step() -- see pumpChat_.
//
// No question and no answer means no: a turn that is cancelled while a question is
// open simply never runs the tool, which is the safe direction for the mistake.


// What the user made of a confirmation request. The third answer is not "allow
// this kind of thing": it is this call, tool and arguments included, for the
// rest of the run - the same key the plan cache uses, so a build loop that runs
// `make test` forty times asks once and not forty times.
enum ApprovalAnswer { apNo = 0, apOnce = 1, apSession = 2 };

// The approval box, with one reflex of its own: if a stop was asked for -- terminal
// closed, `kill`, Ctrl-C -- close the dialog with cmNo on the next event.
//
// It has to be here rather than on the tick, because the tick is the code that is
// sitting inside this dialog: `pumpChat_()` opens it from the timer handler and blocks in
// `execView`, and a second tick cannot walk back into the pump (`g_inStep_` says no) --
// so while the box is up, nothing else in atui is going to look at the flag.  What does
// arrive is tvision's own traffic to the modal group: measured with an instrumented
// dialog, broadcasts of `evBroadcast` come through within a few hundred milliseconds of
// the box opening, which is what makes the check fire without anybody touching a key.
//
// cmNo because "the program is ending" is not a yes: nothing that had not already been
// cleared before the dialog opened is run on the way out.  And the flag stays set -- the
// tick that runs after this dialog returns finds it and quits through the path that
// cancels a turn and waits for the worker.
//
// Measured against a real endpoint, asked to run `touch /tmp/atui_stop_probe` (outside
// the project, so it asks) and sent a SIGTERM with the box on screen:
//
//     with this class    stopped in 0.3s, status 0, the file never created   (x4)
//     without it         still waiting at 8s, needed SIGKILL; file never created
class ApprovalDialog : public TDialog
{
public:
    ApprovalDialog( const TRect &bounds, TStringView title )
        : TWindowInit( &TDialog::initFrame ),
          TDialog( bounds, title )
    {
    }

    virtual void handleEvent( TEvent &event )
    {
        if( event.what != evNothing && atui::core::shutdownRequested() )
        {
            clearEvent( event );
            endModal( cmNo );
            return;
        }
        TDialog::handleEvent( event );
    }
};

// Show the confirmation dialog for a pending tool approval. Modal: it runs a
// nested event loop on this thread.
//
// Not stock messageBox() - that sizes itself for about three lines of text and
// silently clips the rest, which here would hide the one line saying what is
// actually about to run. This box is laid out around the message instead, and
// No is the default button, so Enter and Esc both decline.
int showApprovalDialog_( const atui::tools::ApprovalRef &req )
{
    // The keys are spelled out because the box is safe by default: Enter and
    // Esc decline, so approving has to be a deliberate keystroke.
    std::string body = "The model wants to run the \"" + req->tool +
                       "\" tool.\n\n" + req->summary;
    // Why it is asking in the first place: the same sentence the transcript
    // records and the log writes, so "what rule put this in front of me" gets
    // answered where the decision is made.
    if( !req->reason.empty() )
        body += "\n\nWhy it is asking: " + req->reason;
    body += "\n\nAllow this to run?\n"
            "(Alt-Y yes, Alt-R yes and remember this exact command, Enter no)";

    // Wide enough to show a command, narrow enough to keep the frame on screen.
    const short deskW = TProgram::deskTop ? TProgram::deskTop->size.x : 80;
    const short deskH = TProgram::deskTop ? TProgram::deskTop->size.y : 25;
    short textW = 64;
    if( textW > deskW - 12 ) textW = (short) ( deskW - 12 );
    if( textW < 24 )         textW = 24;

    std::vector<std::string> rows = wrapText_( body, textW );
    // Never taller than the desktop. The command is early in the message, so an
    // extreme clip loses the closing question rather than the command.
    short maxRows = (short) ( deskH - 8 );
    if( maxRows < 1 ) maxRows = 1;
    if( (short) rows.size() > maxRows )
    {
        rows.resize( maxRows );
        rows.back() += "...";
    }

    std::string text;
    for( size_t i = 0; i < rows.size(); ++i )
    {
        if( i != 0 ) text += "\n";
        text += rows[i];
    }
    // Folded here, once, rather than at each of the pieces above: everything the dialog
    // shows comes off the network -- the tool's own words, the judge's reason, the
    // command the model wrote -- and this is the last point before it reaches a terminal
    // whose character set may be an 8-bit codepage.
    text = atui::tui::foldForTerminal( text );

    const short w = (short) ( textW + 6 );
    const short h = (short) ( (short) rows.size() + 5 );
    const std::string title = atui::tui::foldForTerminal( "Confirm - " + req->tool );
    ApprovalDialog *d = new ApprovalDialog( TRect( 0, 0, w, h ), title.c_str() );
    d->options |= ofCentered;
    d->insert( new TStaticText( TRect( 3, 1, (short) ( w - 2 ), (short) ( h - 4 ) ),
                                text.c_str() ) );
    // Buttons. TVision gives a dialog's initial focus to the view inserted
    // LAST, and a focused TButton becomes the default (TButton::setState calls
    // makeDefault on sfFocused) - so the declining button goes in last, and
    // Enter and Esc both decline while Alt-Y allows.
    //
    // The session button needs three button widths of room; on a terminal too
    // narrow for that the dialog degrades to yes/no rather than overlapping.
    const bool room = ( w >= 50 );
    short xSession = (short) ( w - 37 );
    short xYes     = room ? xSession : (short) ( w - 25 );
    d->insert( new TButton( TRect( xYes, (short) ( h - 3 ),
                                   (short) ( xYes + 10 ), (short) ( h - 1 ) ),
                            "~Y~es", cmYes, bfNormal ) );
    if( room )
    {
        d->insert( new TButton( TRect( (short) ( w - 26 ), (short) ( h - 3 ),
                                       (short) ( w - 15 ), (short) ( h - 1 ) ),
                                // Alt-S is the "Switch model" menu, so this
                                // button is "Remember", on a keystroke that is
                                // free. Its command has to be one of the four
                                // TDialog recognises as ending a dialog
                                // (cmOK/cmCancel/cmYes/cmNo) - anything else is
                                // swallowed and the button does nothing - and
                                // cmOK is the one this box does not otherwise
                                // use.
                                "~R~emember", cmOK, bfNormal ) );
    }
    d->insert( new TButton( TRect( (short) ( w - 14 ), (short) ( h - 3 ),
                                   (short) ( w - 4 ), (short) ( h - 1 ) ),
                            "~N~o", cmNo, bfDefault ) );

    // The desktop, not the application group: this is what every other dialog in
    // atui does, and it is what makes the box clickable - a view executed on
    // TProgram::application gets keyboard but its mouse hit-testing is off by the
    // desktop's own frame, so clicks land nowhere.
    const ushort r = TProgram::deskTop
        ? TProgram::deskTop->execView( d )
        : TProgram::application->execView( d );
    TObject::destroy( d );
    if( r == cmYes ) return apOnce;
    if( r == cmOK )  return apSession;
    return apNo;
}

#endif // ATUI_HAVE_NETWORK

// The chat transcript. Replaces the plain TTerminal so we can render the
// model's answers as styled markdown (headings, emphasis, syntax-highlighted
// code blocks, lists, tables...), show the reasoning/"thinking" stream as
// collapsible blocks, keep a lasting record of every tool call ("$ command"
// plus its result, collapsible when the output is long), and toggle a block
// with a mouse click or Ctrl-T. Derives from TScroller for free scrollbar +
// scrolling. Rendering is span-based: each row is a vector of Spans whose Role
// mapColor() turns into a color.
class TranscriptView : public TScroller
{
public:
    TranscriptView( const TRect& bounds, TScrollBar *vBar ) noexcept :
        TScroller( bounds, 0, vBar )
    {
        options &= ~ofSelectable;   // never focusable: typing stays in the input
        eventMask |= (evMouseDown | evMouseWheel);
        expandAll_ = false;
        follow_ = true;
        firstDirty_ = 0;
        batchDepth_ = 0;
        layoutDirty_ = false;
        restartLayout_ = false;
    }

    // --- document building (called on the UI thread) ---------------------

    // A tick drains up to 64 records at once. Between these two calls the appends
    // accumulate and the batch's end lays out and repaints once, rather than sixty
    // times. It is the other half of the fix for the 5 second tick handler measured on
    // the box: incremental layout makes a rebuild cheap, batching makes it happen once.
    void beginBatch() { ++batchDepth_; }
    void endBatch()
    {
        if( batchDepth_ > 0 )
            --batchDepth_;
        if( batchDepth_ == 0 && layoutDirty_ )
            relayout();
    }

    void appendUser( const std::string &text )
    {
        closeOpen();
        Block b; b.kind = kUser; b.text = text; b.open = false; b.expanded = false;
        blocks_.push_back( b );
        follow_ = true;
        changed_( blocks_.size() - 1 );
    }

    void appendStatus( const std::string &text )
    {
        closeOpen();
        Block b; b.kind = kStatus; b.text = text; b.open = false; b.expanded = false;
        blocks_.push_back( b );
        changed_( blocks_.size() - 1 );
    }

    // A tool call is being made: start a record of it. Unlike a status line this
    // stays in the transcript, so what was run - and what it did - remains
    // visible afterwards. The record stays open until its result arrives, which
    // is what makes a slow command visible while it runs.
    void appendTool( const std::string &preview )
    {
        closeOpen();
        Block b;
        b.kind = kTool; b.text = preview; b.open = true; b.expanded = false;
        blocks_.push_back( b );
        changed_( blocks_.size() - 1 );
    }

    // Who let the open tool record through, and why. Kept separate from the
    // command line because the two arrive at different moments: the call is
    // announced, then decided, then run - and the record should say which rule
    // answered, or a permission system is indistinguishable from a lucky guess.
    void appendToolNote( const std::string &note )
    {
        if( blocks_.empty() || blocks_.back().kind != kTool ||
            !blocks_.back().open )
            return;
        blocks_.back().note = note;
        changed_( blocks_.size() - 1 );
    }

    // The outcome of the tool call whose record is still open. A result that
    // arrives with no open record (e.g. after a failed turn) becomes its own.
    void appendToolResult( const std::string &result )
    {
        if( blocks_.empty() || blocks_.back().kind != kTool ||
            !blocks_.back().open )
            appendTool( "(tool result)" );
        blocks_.back().out = result;
        closeOpen();
        changed_( blocks_.size() - 1 );
    }

    void appendThinking( const std::string &chunk )
    {
        if( blocks_.empty() || blocks_.back().kind != kThinking ||
            !blocks_.back().open )
        {
            closeOpen();
            Block b; b.kind = kThinking; b.open = true;
            b.expanded = expandAll_;
            blocks_.push_back( b );
        }
        blocks_.back().text += chunk;
        changed_( blocks_.size() - 1 );
    }

    void appendAnswer( const std::string &chunk )
    {
        if( blocks_.empty() || blocks_.back().kind != kAnswer ||
            !blocks_.back().open )
        {
            closeOpen();
            Block b; b.kind = kAnswer; b.open = true; b.expanded = false;
            blocks_.push_back( b );
        }
        blocks_.back().text += chunk;
        changed_( blocks_.size() - 1 );
    }

    void finishReply()
    {
        closeOpen();
        changed_( blocks_.empty() ? 0 : blocks_.size() - 1 );
    }

    // Toggle every thinking block (Ctrl-T).
    void toggleThinkingAll()
    {
        expandAll_ = !expandAll_;
        size_t first = blocks_.size();
        for( size_t i = 0; i < blocks_.size(); ++i )
            if( blocks_[i].kind == kThinking )
            {
                blocks_[i].expanded = expandAll_;
                if( i < first ) first = i;
            }
        changed_( first );
    }

    // Reflect a persisted default without flipping it (called once at startup).
    void setThinkingExpandAll( bool expand )
    {
        expandAll_ = expand;
        for( size_t i = 0; i < blocks_.size(); ++i )
            if( blocks_[i].kind == kThinking )
                blocks_[i].expanded = expandAll_;
    }

    // --- TView / TScroller overrides -------------------------------------

    virtual void draw()
    {
        if( layoutDirty_ )
            relayout();   // painted while a batch is open: relayout does not repaint one
        int top = delta.y;
        for( int row = 0; row < size.y; ++row )
        {
            int li = top + row;
            // Fill the whole row with its background role first. Code rows use
            // rCode so the code background spans edge-to-edge (and blank lines
            // inside a fence still read as code) rather than only under text.
            unsigned char bgRole = ( li >= 0 && li < (int) lines_.size() )
                                       ? lines_[li].bg : rNormal;
            writeChar( 0, row, ' ', bgRole, (ushort) size.x );
            if( li < 0 || li >= (int) lines_.size() ) continue;
            int x = 0;
            const std::vector<Span> &spans = lines_[li].spans;
            for( size_t s = 0; s < spans.size() && x < size.x; ++s )
            {
                if( !spans[s].text.empty() )
                    writeStr( (short) x, (short) row,
                              spans[s].text.c_str(), spans[s].role );
                x += (int) spans[s].text.size();
            }
        }
    }

    virtual void handleEvent( TEvent &event )
    {
        if( event.what == evMouseWheel )
        {
            // A notch moves as many lines as the user set (Options >
            // Scrolling); one line reads as stalled next to how fast a wheel
            // turns.
            const int step = atui::settings::wheelLines();
            int d = ( event.mouse.wheel & mwDown ) ? step
                  : ( ( event.mouse.wheel & mwUp ) ? -step : 0 );
            if( d != 0 )
            {
                int maxTop = (int) lines_.size() - size.y;
                if( maxTop < 0 ) maxTop = 0;
                int nt = delta.y + d;
                if( nt < 0 ) nt = 0;
                if( nt > maxTop ) nt = maxTop;
                follow_ = ( nt >= maxTop );
                if( nt != delta.y ) scrollTo( 0, nt );
                clearEvent( event );
                return;
            }
        }
        else if( event.what == evMouseDown &&
                 ( event.mouse.buttons & mbLeftButton ) )
        {
            int row = event.mouse.where.y - origin.y;
            int li = delta.y + row;
            if( li >= 0 && li < (int) lines_.size() && lines_[li].toggle )
            {
                int b = lines_[li].block;
                if( b >= 0 && b < (int) blocks_.size() &&
                    ( blocks_[b].kind == kThinking ||
                      blocks_[b].kind == kTool ) )
                {
                    blocks_[b].expanded = !blocks_[b].expanded;
                    changed_( (size_t) b );
                    clearEvent( event );
                    return;
                }
            }
        }
        TScroller::handleEvent( event );
    }

    // The scrollbar moved the view (click or drag on the bar). Keep the
    // "follow the bottom" flag in step with it: jumping up stays up, and
    // returning the thumb to the end resumes following the stream. Without
    // this, only the wheel kept follow_ correct and a bar-clicked-away view
    // still yanked back to the bottom on the next repaint.
    virtual void scrollDraw()
    {
        TScroller::scrollDraw();
        int maxTop = (int) lines_.size() - size.y;
        if( maxTop < 0 ) maxTop = 0;
        follow_ = ( delta.y >= maxTop );
    }

    // Re-clamp the scroll position after the view was resized (e.g. the prompt
    // grew and this view lost rows) and repaint.
    void reflow()
    {
        int maxTop = (int) lines_.size() - size.y;
        if( maxTop < 0 ) maxTop = 0;
        if( follow_ || delta.y > maxTop )
            delta.y = maxTop;
        setLimit( 0, (int) lines_.size() );
        drawView();
    }

    virtual void changeBounds( const TRect &bounds )
    {
        TScroller::changeBounds( bounds );
        // Every wrap depends on the width, so nothing laid out at the old size can be
        // kept -- which is exactly what restartLayout_ is for.
        restartLayout_ = true;
        relayout();
    }

    virtual void setState( ushort aState, Boolean enable )
    {
        TScroller::setState( aState, enable );
        if( vScrollBar )
            vScrollBar->show();
    }

    virtual TColorAttr mapColor( uchar index )
    {
        // Roles are the styling vocabulary (see md_style.h); what a role looks
        // like is the theme's business (see theme.h).
        return roleAttr( Role( index ) );
    }

private:
    enum Kind { kUser, kAnswer, kThinking, kStatus, kTool };
    struct Block {
        int kind;
        std::string text;    // user text / answer / thinking / tool preview
        std::string out;     // tool blocks: what the tool returned
        std::string note;    // tool blocks: who allowed it and why, if anything
        bool expanded;
        bool open;
    };
    struct RLine {
        std::vector<Span> spans; int block; bool toggle;
        unsigned char bg;                    // whole-row background role
        RLine() : spans(), block( -1 ), toggle( false ), bg( rNormal ) {}
        RLine( const std::vector<Span> &s, int b, bool g,
               unsigned char bgRole = rNormal )
            : spans( s ), block( b ), toggle( g ), bg( bgRole ) {}
    };

    // A single-span row (used for user / status / thinking text).
    static RLine plainLine( const std::string &t, unsigned char role,
                            int b, bool g )
    {
        RLine r; r.spans.push_back( Span( t, role ) );
        r.block = b; r.toggle = g; return r;
    }

    // Maximum height of a collapsed tool record (command, result and hint
    // together). Anything longer starts collapsed and is expanded by a click.
    enum { kToolRows = 5 };

    // Drop blank rows at both ends (tool output usually ends in a newline and
    // the model-facing result starts with one after the exit code).
    static void trimBlankRows_( std::vector<std::string> &v )
    {
        size_t a = 0, b = v.size();
        while( a < b && v[a].find_first_not_of( " \t" ) == std::string::npos ) ++a;
        while( b > a && v[b - 1].find_first_not_of( " \t" ) == std::string::npos )
            --b;
        v.erase( v.begin() + b, v.end() );
        v.erase( v.begin(), v.begin() + a );
    }

    // The body of an expanded thinking block. Everything about how reasoning
    // looks - plain prose, code blocks like an answer's, three-column indent -
    // lives in tui/thinking.cpp, where it can be tested without a terminal.
    void emitThinking_( const std::string &text, int b, int width )
    {
        std::vector<Line> body = atui::tui::renderThinkingBody( text, width );
        for( size_t i = 0; i < body.size(); ++i )
            lines_.push_back( RLine( body[i].spans, b, true, body[i].bg ) );
    }

    void closeOpen()
    {
        for( size_t i = 0; i < blocks_.size(); ++i )
        {
            if( !blocks_[i].open )
                continue;
            blocks_[i].open = false;
            // An open block renders differently from a closed one -- a tool record
            // says what is still running -- so closing one dirties it.
            if( i < firstDirty_ ) firstDirty_ = i;
        }
    }

    void relayout()
    {
        layoutDirty_ = false;
        const int width = size.x > 0 ? size.x : 1;

        // Where the rebuild starts. `lines_` is the wrapped form of `blocks_`, and
        // almost every change is at the end of it: a streamed chunk extends the last
        // block, a result completes one, a new record adds one. For those the rows up to
        // the changed block are kept and only that block's own rows -- and anything after
        // it -- are re-wrapped. `restartLayout_` says nothing can be kept, which is what
        // a resize does.
        //
        // Before this, every chunk of every answer re-wrapped the whole transcript and
        // repainted the whole view. On the box, a 250-line streamed answer spent 5.05
        // seconds in one tick handler, and the keyboard was dead for as long as the model
        // kept talking (TESTING.md, "The transcript was the freeze").
        size_t first = firstDirty_;
        if( restartLayout_ || first == 0 || first > rowsEnd_.size() )
        {
            first = 0;
            lines_.clear();
            rowsEnd_.clear();
        }
        else
        {
            // `rowsEnd_[i]` covers blocks 0..i, so keeping blocks 0..first-1 means
            // keeping `first` entries -- and truncating the rows to where entry
            // `first-1` ended. Getting this by one keeps the old copy of the changed
            // block's rows while appending the new one, which is a transcript that
            // repeats the answer: caught by counting copies of the answer's tail on
            // screen before and after this change (2 before the fix below, 1 after).
            lines_.resize( rowsEnd_[ first - 1 ] );
            rowsEnd_.resize( first );
        }
        restartLayout_ = false;

        for( size_t b = first; b < blocks_.size(); ++b )
        {
            if( !lines_.empty() )
                lines_.push_back( RLine() );   // blank spacer between blocks
            const Block &bl = blocks_[b];
            // What the terminal is shown, which for an 8-bit console is not what
            // arrived. Folded here, at the last point before the screen, rather than on
            // the way in, because a streamed answer arrives in chunks and a multi-byte
            // character can be split between two of them -- by now the block holds the
            // whole text. Blocks keep what was sent so a copy, an export or a fold
            // toggle sees the original.
            const std::string text = atui::tui::foldForTerminal( bl.text );
            const std::string out  = atui::tui::foldForTerminal( bl.out );
            const std::string note = atui::tui::foldForTerminal( bl.note );
            if( bl.kind == kUser )
            {
                std::vector<std::string> w =
                    wrapText_( "> " + text, width );
                for( size_t i = 0; i < w.size(); ++i )
                    lines_.push_back( plainLine( w[i], rUser, (int) b, false ) );
            }
            else if( bl.kind == kStatus )
            {
                std::vector<std::string> w = wrapText_( text, width );
                for( size_t i = 0; i < w.size(); ++i )
                    lines_.push_back( plainLine( w[i], rStatus, (int) b, false ) );
            }
            else if( bl.kind == kTool )
            {
                // "$ the command" (continuation lines indented under it), then
                // what it returned. Collapsed, a record is kept to kToolRows
                // rows in total so one chatty command cannot push the
                // conversation off screen: the command first - it *is* the
                // record, so it is never cut - then as much of the result as
                // fits, then the hint that expands it.
                int  outW = ( width > 12 ) ? width - 2 : width;
                std::vector<std::string> rl = wrapText_( out, outW );
                trimBlankRows_( rl );

                std::string cmd = "$ ";
                for( size_t i = 0; i < text.size(); ++i )
                {
                    if( text[i] == '\n' ) cmd += "\n  ";
                    else                   cmd += text[i];
                }
                std::vector<std::string> cl = wrapText_( cmd, width );

                // The permission note, on its own row, marked like a comment so
                // it cannot be mistaken for the command or for its output.
                std::vector<std::string> nl;
                if( !note.empty() )
                    nl = wrapText_( "  # " + note, width );

                bool   foldable = cl.size() + nl.size() + rl.size()
                                  > (size_t) kToolRows;
                size_t shown = rl.size();
                if( foldable && !bl.expanded )
                {
                    // -1: the hint. The command and its note are never cut: they
                    // are what was decided, the result is commentary.
                    int room = kToolRows - (int) cl.size() - (int) nl.size() - 1;
                    shown = ( room > 0 ) ? (size_t) room : 0;
                    if( shown > rl.size() ) shown = rl.size();
                }

                for( size_t i = 0; i < cl.size(); ++i )
                    lines_.push_back( plainLine( cl[i], rTool, (int) b,
                                                 foldable ) );
                for( size_t i = 0; i < nl.size(); ++i )
                    lines_.push_back( plainLine( nl[i], rToolOut, (int) b,
                                                 foldable ) );
                for( size_t i = 0; i < shown; ++i )
                    lines_.push_back( plainLine( "  " + rl[i], rToolOut,
                                                 (int) b, foldable ) );
                if( foldable )
                {
                    std::string hint = bl.expanded
                        ? "  (click to collapse)"
                        : "  .. " + atui::core::toString(
                              (unsigned long) (rl.size() - shown) )
                          + " more lines (click to expand)";
                    lines_.push_back( plainLine( hint, rHead, (int) b, true ) );
                }
            }
            else if( bl.kind == kAnswer )
            {
                // Render as markdown: headings, emphasis, lists, tables and
                // syntax-highlighted code blocks become styled spans.
                std::vector<Line> md = atui::tui::render( text, width );
                for( size_t i = 0; i < md.size(); ++i )
                    lines_.push_back(
                        RLine( md[i].spans, (int) b, false, md[i].bg ) );
            }
            else // kThinking
            {
                std::string head = bl.expanded ? "[-] thinking ("
                                               : "[+] thinking (";
                head += atui::core::toString( (unsigned long) bl.text.size() );
                head += bl.expanded ? " chars)  click/Ctrl-T to collapse"
                                    : " chars)  click/Ctrl-T to expand";
                std::vector<std::string> hw = wrapText_( head, width );
                for( size_t i = 0; i < hw.size(); ++i )
                    lines_.push_back( plainLine( hw[i], rHead, (int) b, true ) );
                if( bl.expanded )
                    emitThinking_( text, (int) b, width );
            }
            rowsEnd_.push_back( lines_.size() );
        }

        firstDirty_ = blocks_.size();

        int contentH = (int) lines_.size();
        setLimit( 0, contentH );
        int maxTop = contentH - size.y;
        if( maxTop < 0 ) maxTop = 0;
        if( follow_ || delta.y > maxTop )
            scrollTo( 0, maxTop );
        // While a batch is open the caller is still appending; it repaints once at the
        // end of the batch instead of once per record.
        if( batchDepth_ == 0 )
            drawView();
    }

    // The earliest block whose rows in `lines_` are no longer current, and the row each
    // block ends at -- which is what lets a rebuild start in the middle. Blocks are only
    // added at the end and never removed or reordered, so a block index stays meaningful
    // for the life of the transcript.
    void changed_( size_t fromBlock )
    {
        if( fromBlock < firstDirty_ )
            firstDirty_ = fromBlock;
        if( batchDepth_ == 0 )
            relayout();
        else
            layoutDirty_ = true;
    }

    std::vector<Block> blocks_;
    std::vector<RLine> lines_;
    std::vector<size_t> rowsEnd_;
    size_t firstDirty_;
    int    batchDepth_;
    bool   layoutDirty_;
    bool   restartLayout_;
    bool expandAll_;
    bool follow_;
};


// --- PromptInput: a wrapping, auto-growing prompt ----------------------------
//
// The classic TInputLine cannot wrap: it keeps one screen column per character
// and its draw() just repaints that single row. So we implement the prompt our-
// selves. It stores a byte buffer (the transcript layout model throughout this
// app is "1 byte == 1 column") and soft-wraps it into rows of `width` columns,
// wrapping on spaces where possible. The panel grows and shrinks to fit those
// rows; when the text is longer than the panel allows (see relayoutPanels_) it
// scrolls vertically and keeps the caret visible.
//
// Row 0 of the view is a horizontal separator drawn between the transcript
// above and the text below.
class PromptInput : public TView
{
public:
    PromptInput( const TRect &bounds ) noexcept
        : TView( bounds ), cursor_( 0 ), top_( 0 )
    {
        options |= ofSelectable;
        eventMask |= evMouseDown;
        // TView does NOT ask for a text caret - its state is just sfVisible, and
        // TView::resetCursor() (computeCaretSize) only places the terminal caret
        // when sfVisible|sfCursorVis|sfFocused are all set. Widgets that take
        // text set the flag themselves (see TInputLine), so a custom editor has
        // to call showCursor() or setCursor() silently does nothing.
        showCursor();
    }

    // The buffer contents (bytes).
    const std::string &text() const { return buf_; }
    void setText( const std::string &t )
    {
        buf_ = t;
        if( cursor_ > buf_.size() ) cursor_ = buf_.size();
    }
    void clear() { buf_.clear(); cursor_ = 0; top_ = 0; }

    // Number of wrapped rows the current text needs at the given width.
    int rowCount( int w )
    {
        rewrap( w );
        return (int) rows_.size();
    }

    // Byte width available for text (the view's full width).
    int columnWidth() const { return size.x > 0 ? size.x : 1; }

    virtual void draw()
    {
        // Fill everything, then paint row 0 (the separator) and the text rows.
        for( int r = 0; r < size.y; ++r )
            writeChar( 0, (short) r, ' ', (uchar) rInput, (ushort) size.x );

        if( size.y >= 1 )
            writeChar( 0, 0, '-', (uchar) rInputSep, (ushort) size.x );

        rewrap( columnWidth() );

        // Keep the caret on a visible wrapped row (row 0 is the separator).
        int visRows = size.y - 1;
        if( visRows < 1 ) visRows = 1;
        int cr = 0, cc = 0;
        caretPos( cr, cc );
        // A row that is exactly as wide as the view leaves no cell at column
        // size.x, and a caret outside the view is dropped instead of drawn.
        if( cc > size.x - 1 ) cc = size.x - 1;
        // Don't allow scrolling past the last wrapped row (e.g. right after the
        // panel grew and top_ was left over from a shorter height).
        int maxTop = (int) rows_.size() - visRows;
        if( maxTop < 0 ) maxTop = 0;
        if( top_ > cr ) top_ = cr;
        if( cr >= top_ + visRows ) top_ = cr - visRows + 1;
        if( top_ > maxTop ) top_ = maxTop;
        if( top_ < 0 ) top_ = 0;

        for( int r = 0; r < visRows; ++r )
        {
            int idx = top_ + r;
            if( idx >= 0 && idx < (int) rows_.size() )
            {
                const std::string &row = rows_[idx];
                if( !row.empty() )
                    writeStr( 0, (short)(r + 1), row.c_str(), (uchar) rInput );
            }
        }

        if( (state & sfFocused) && cr >= top_ && cr < top_ + visRows )
            setCursor( cc, (short)(cr - top_ + 1) );
        else
            // Park the caret outside the view instead of calling hideCursor():
            // that clears sfCursorVis permanently (only TInputLine ever puts it
            // back), so the caret would never return after the first refocus.
            setCursor( -1, -1 );
    }

    virtual void handleEvent( TEvent &event )
    {
        if( event.what == evMouseDown )
        {
            focus();
            clearEvent( event );
            return;
        }
        if( event.what != evKeyDown ) { TView::handleEvent( event ); return; }

        ushort c = event.keyDown.keyCode;
        int w = columnWidth();
        bool handled = true;

        // Navigation / editing keys (keyCode is reliable for these on every
        // platform).
        switch( c )
        {
            case kbLeft:  if( cursor_ > 0 ) cursor_ = prevChar( cursor_ ); break;
            case kbRight: if( cursor_ < buf_.size() ) cursor_ = nextChar( cursor_ ); break;
            case kbHome:  cursor_ = 0; break;
            case kbEnd:   cursor_ = buf_.size(); break;
            case kbBack:
                if( cursor_ > 0 ) { size_t p = prevChar( cursor_ ); buf_.erase( p, cursor_ - p ); cursor_ = p; }
                break;
            case kbDel:
                if( cursor_ < buf_.size() ) { buf_.erase( cursor_, nextChar( cursor_ ) - cursor_ ); }
                break;
            case kbUp:    moveVert( -1, w ); break;
            case kbDown:  moveVert(  1, w ); break;
            default:
            {
                // Printable text. Read it from the UTF-8 text buffer rather than
                // keyCode: many terminals (and bracketed paste) deliver the
                // character only in `text` with keyCode == 0. Control bytes are
                // skipped so Enter/Esc/accelerators still bubble up to TApp.
                if( event.keyDown.controlKeyState & (kbCtrlShift | kbAltShift) )
                {
                    handled = false;    // leave Ctrl/Alt combos to the app/menu
                    break;
                }
                TStringView txt = event.keyDown.getText();
                bool inserted = false;
                for( int i = 0; i < (int) event.keyDown.textLength; ++i )
                {
                    unsigned char b = (unsigned char) txt[i];
                    if( b < 32 ) continue;                 // control (incl. Enter)
                    buf_.insert( cursor_, 1, (char) b );
                    ++cursor_;
                    inserted = true;
                }
                handled = inserted;
                break;
            }
        }

        if( handled )
        {
            relayoutPanels_();
            clearEvent( event );
        }
        else
            TView::handleEvent( event );
    }

protected:
    // Caret position as (wrapped row, column) using the current rows_.
    void caretPos( int &row, int &col )
    {
        row = 0; col = 0;
        for( int i = 0; i < (int) rows_.size(); ++i )
        {
            size_t start = rowStart( i );
            size_t end = start + rows_[i].size();
            if( cursor_ <= end )
            {
                row = i;
                col = (int)(cursor_ - start);
                if( col < 0 ) col = 0;
                return;
            }
            row = i;                        // fallback: last row
        }
        if( !rows_.empty() )
        {
            size_t start = rowStart( rows_.size() - 1 );
            col = (int)(cursor_ - start);
            if( col < 0 ) col = 0;
        }
    }

    // Move the caret up/down one visual row, keeping the column where possible.
    void moveVert( int dir, int w )
    {
        rewrap( w );
        int row, col;
        caretPos( row, col );
        int target = row + dir;
        if( target < 0 ) { cursor_ = 0; return; }
        if( target >= (int) rows_.size() )
        {
            if( dir > 0 ) cursor_ = buf_.size();
            return;
        }
        size_t start = rowStart( target );
        size_t len = rows_[target].size();
        if( (size_t) col > len ) col = (int) len;
        cursor_ = start + col;
    }

    // Byte offset where wrapped row i begins. This has to come from the table
    // rewrap() builds, not from summing the row lengths: a wrap that breaks on
    // a space drops that space from the row, so the lengths no longer add up to
    // the buffer offsets (each dropped space shifted the caret one column left
    // on every wrapped row below it).
    size_t rowStart( int i )
    {
        if( i >= 0 && i < (int) rowOfs_.size() )
            return rowOfs_[i];
        return buf_.size();
    }

    // UTF-8 aware caret stepping: treat continuation bytes (0x80..0xBF) as part
    // of the preceding code point so Backspace/Delete and arrow keys move by a
    // whole character. (Matches the app's "1 byte == 1 column" layout, which is
    // exact for ASCII and degrades gracefully for wider scripts.)
    size_t prevChar( size_t p )
    {
        if( p == 0 ) return 0;
        size_t q = p - 1;
        while( q > 0 && ((unsigned char) buf_[q] & 0xC0) == 0x80 ) --q;
        return q;
    }
    size_t nextChar( size_t p )
    {
        size_t n = buf_.size();
        if( p >= n ) return n;
        size_t q = p + 1;
        while( q < n && ((unsigned char) buf_[q] & 0xC0) == 0x80 ) ++q;
        return q;
    }

    // Greedy word-wrap of buf_ into rows_ (each row without the trailing space).
    // The buffer is small (a single prompt line), so we just rebuild every time
    // rather than cache — a width-keyed cache goes stale the moment buf_ changes
    // from anywhere other than this function.
    void rewrap( int w )
    {
        rows_.clear();
        rowOfs_.clear();
        if( w < 1 ) w = 1;
        if( buf_.empty() ) { rows_.push_back( std::string() ); rowOfs_.push_back( 0 ); return; }

        size_t i = 0, n = buf_.size();
        while( i < n )
        {
            if( (int)(n - i) <= w )
            {
                rows_.push_back( buf_.substr( i ) );
                rowOfs_.push_back( i );
                break;
            }
            size_t brk = i + w;               // one past the last column
            int first = (int) i;              // earliest break is after 1 char
            size_t sp = buf_.rfind( ' ', brk );
            size_t rowEnd;
            if( sp != std::string::npos && (int) sp > first && sp < brk )
                rowEnd = sp;                    // break on the last space
            else
                rowEnd = brk;                   // hard wrap
            rows_.push_back( buf_.substr( i, rowEnd - i ) );
            rowOfs_.push_back( i );
            i = rowEnd;
            if( i < n && buf_[i] == ' ' ) ++i;  // skip the break space
        }
    }

    virtual TColorAttr mapColor( uchar index )
    {
        return roleAttr( Role( index ) );
    }

    std::string              buf_;
    size_t                   cursor_;
    int                      top_;         // first visible wrapped row
    std::vector<std::string> rows_;        // wrapped display rows
    std::vector<size_t>      rowOfs_;      // byte offset each wrapped row starts at
};



class TApp : public TApplication
{
public:
    TApp()
        : TProgInit( &TApp::initStatusLine,
                      &TApp::initMenuBar,
                      &TApp::initDeskTop )
    {
        g_stub_ = new atui::core::ChatModelStub();
#ifdef ATUI_DEBUG_KEYS
        g_keylog = std::ofstream( "/tmp/atui_keylog.txt" );
        if( !g_keylog ) g_keylog << "keylog open failed\n";
#endif
    }

    ~TApp()
    {
        delete g_stub_;
        g_stub_ = 0;
#ifdef ATUI_HAVE_NETWORK
        // A turn still in flight at exit: refuse whatever it is waiting on, so it
        // stops waiting, then wait for the stack to finish with it -- the task and the
        // turn it holds are both about to go away.  This can take as long as a tool
        // call takes, because a child process is not interruptible: the same truth
        // `ChatTurn::cancel()` documents, arriving at exit instead of at Escape.
        if( g_turnThread_.running() )
        {
            // Requests, both of them, rather than cancellations: `requestStop()` is
            // checked between passes, `declineUnanswered()` releases a worker parked on
            // a dialog.  A bare join here was tried first, and it meant that quitting
            // during a turn waited for the model's answer before the process would close
            // -- which is indistinguishable from a hang to whoever is typing.
            g_queue_.requestStop();
            g_queue_.declineUnanswered();
            g_turnThread_.join();
        }
        delete g_turn_;
        g_turn_ = 0;
#endif
    }

    virtual void handleEvent( TEvent& event )
    {
        // Ctrl-T: expand/collapse all thinking blocks. Intercept before the
        // focused input line can consume the control character. Match the
        // control byte (0x14); the keyCode is (scancode<<8 | 0x14).
        if( event.what == evKeyDown &&
            ( event.keyDown.keyCode & 0x00FF ) == ( kbCtrlT & 0x00FF ) )
        {
            if( g_transcript_ )
                g_transcript_->toggleThinkingAll();
            clearEvent( event );
            return;
        }

        TApplication::handleEvent( event );

#ifdef ATUI_DEBUG_KEYS
        g_keylog << "what=" << (int)event.what
                 << " command=" << (int)event.message.command
                 << " keyCode=" << (int)event.keyDown.keyCode
                 << " mods=" << (int)event.keyDown.controlKeyState
                 << " text=" << (event.keyDown.textLength ? std::string(event.keyDown.text, (size_t)event.keyDown.textLength) : std::string(""))
                 << "\n";
        g_keylog.flush();
#endif

        // Periodic: poll the AI token queue and keep the prompt/transcript
        // split in sync with the desktop (cheap no-op when nothing changed, so
        // it also self-heals after a terminal resize).
        if( event.what == evBroadcast && event.message.command == cmTimerExpired )
        {
            turnDebug_( "tick", 0, 0 );   // before anything can swallow it, so a
                                          // quiet pump can be told from a quiet timer

            // Asked to stop -- the terminal closed, `kill`, Ctrl-C.  Handled here, on
            // the tick, because that is as close to the signal as this program can
            // safely get: the handler that noticed it may set a flag and nothing else
            // (`core/signals.h`).  Quitting goes through the same endModal Alt-X uses,
            // so a turn in flight is cancelled and waited for, and the screen is put
            // back by the code that took it over instead of by a handler halfway
            // through a write to a terminal nobody is reading.  This runs with a
            // dialog open too, and `endModal` here ends the program from under it
            // (measured: an approval question on screen, SIGTERM, gone in 0.3s with
            // status 0 and the tool never run).
            if( atui::core::shutdownRequested() )
            {
                // Not cleared.  Nothing here is done with the request yet: leaving
                // runs the TApp destructor, which waits for a worker that may be
                // sitting in a tool's child process or a socket read, and those
                // checks are what makes the difference between leaving and waiting
                // for the command.  Clearing it here once cost 40s of waiting for a
                // `sleep 40` whose kill should have been immediate (measured: the
                // tick saw the flag, cleared it, and the worker polled a flag that
                // had already been forgiven).
                turnDebug_( "stop", 0, 0 );
                clearEvent( event );
                endModal( cmQuit );
                return;
            }

            // The scripted exit, if it was armed (see quitAfterMs_()).
            if( g_quitAfterMs_ )
            {
                if( g_firstTickMs_ == 0 ) g_firstTickMs_ = nowMs_();
                else if( nowMs_() - g_firstTickMs_ >= g_quitAfterMs_ )
                {
                    turnDebug_( "quittimer", (long) g_quitAfterMs_,
                                (long) ( nowMs_() - g_firstTickMs_ ) );
                    clearEvent( event );
                    endModal( cmQuit );
                    return;
                }
            }

            // First thing: if the last run died with a file half written, that is
            // more urgent than anything the model is about to say. Runs once.
            maybeRecoverInterruptedWrites_();
            pumpChat_();
            relayoutPanels_();
            clearEvent( event );
            return;
        }

        if( event.what == evCommand )
        {
            const ushort cmd = event.message.command;

            // "Switch model" entry: the command carries the index of the
            // provider whose configured model was picked.
            if( cmd >= CmdSwitchModelBase &&
                cmd < CmdSwitchModelBase + kMaxSwitchModels )
            {
                pickSwitchModel_( cmd - CmdSwitchModelBase );
                clearEvent( event );
                return;
            }

            switch( cmd )
            {
                case CmdHelp:
                    showHelp();
                    clearEvent( event );
                    break;
                case CmdProviders:
                    showProviders();
                    clearEvent( event );
                    break;
                case CmdTheme:
                    showTheme();
                    clearEvent( event );
                    break;
                case CmdSettings:
                    showSettings();
                    clearEvent( event );
                    break;
                case CmdUndo:
                    showUndoDialog_();
                    clearEvent( event );
                    break;
                case cmQuit:
                    clearEvent( event );
                    endModal( cmQuit );
                    break;
                default:
                    break;
            }
        }

        // Intercept Enter in the prompt and route it to the chat flow. Some
        // terminals report Enter as a key-down, others as a command event; the
        // PromptInput deliberately leaves Enter unconsumed so it reaches here.
        // Only act when the prompt itself has focus (not a modal dialog).
        bool enterPressed =
            ( event.what == evKeyDown && event.keyDown.keyCode == kbEnter ) ||
            ( event.what == evCommand  && event.message.command == kbEnter );
        if( g_prompt_ != 0 && enterPressed &&
            (g_prompt_->state & sfFocused) != 0 )
        {
            processMessage_();
            clearEvent( event );
            return;
        }
    }

private:
    static TStatusLine *initStatusLine( TRect r );
    static TMenuBar *initMenuBar( TRect r );
    static TDeskTop *initDeskTop( TRect r );
    void showHelp();
    void showProviders();
    void showTheme();
    void showSettings();

    virtual TPalette& getPalette() const
    {
        if( TProgram::appPalette == apColor )
            return themePalette();
        return TApplication::getPalette();
    }
};

TStatusLine *TApp::initStatusLine( TRect r )
{
    return initStatusLine_( r );
}

TMenuBar *TApp::initMenuBar( TRect r )
{
    return initMenuBar_( r );
}

TDeskTop *TApp::initDeskTop( TRect r )
{
    return initDeskTop_( r );
}

// --- module-level implementations ------------------------------------------------

TStatusLine *initStatusLine_( TRect r )
{
    r.a.y = r.b.y - 1;
    // The first item is the active provider/model; its text is rewritten by
    // updateProviderStatus_() whenever a provider or model is switched. (It is
    // filled in right here: TVision builds the desktop before the status line,
    // so the app's start-up pass would be too early.)
    g_statusProvider_ = new TStatusItem( "  ", 0, 0 );
    // What atui is currently allowed to do without asking. A system that can
    // act on the user's behalf should say so while it is doing it, in one glance,
    // rather than explaining itself after the fact.
    g_statusPermission_ = new TStatusItem( "  ", 0, 0 );
    TStatusLine *line = new TStatusLine( r,
        *new TStatusDef( 0, 0xFFFF )
        + *g_statusProvider_
        + *g_statusPermission_
        + *new TStatusItem( "~Alt-H~  About", kbAltH, CmdHelp )
        + *new TStatusItem( " Ctrl-T  thinking", 0, 0 )
        + *new TStatusItem( "~Alt-X~  Quit",  kbAltX, cmQuit )
    );
    updateProviderStatus_();
    updatePermissionStatus_();
    return line;
}

TMenuBar *initMenuBar_( TRect r )
{
    r.b.y = r.a.y + 1;
    // The "Switch model" dropdown must be a TSubMenu: in a menu-bar item chain,
    // "subMenu + menuItem" appends to the LAST top-level entry's own menu, so
    // only another TSubMenu creates a new top-level dropdown.
    g_switchModelMenu_ = new TSubMenu( "~S~witch model", kbAltS );
    delete g_switchModelMenu_->subMenu;    // the TSubMenu's empty menu
    g_switchModelMenu_->subMenu = buildSwitchModelMenu_();

    return new TMenuBar( r,
        *new TSubMenu( "~F~ile", kbAltF )
        + *new TMenuItem( "H~elp...",  CmdHelp, kbAltH, hcNoContext, "Alt-H" )
        // The journal's front door: every saved copy atui holds, and the two
        // things that can be done with one.
        + *new TMenuItem( "~Undo file...", CmdUndo, kbAltU, hcNoContext, "Alt-U" )
        + newLine()
        + *new TMenuItem( "~O~ptions", kbAltO,
              new TMenu(
                  *new TMenuItem( "~P~roviders...", CmdProviders, kbAltP,
                                  hcNoContext, "Alt-P" )
                  + *new TMenuItem( "Sett~i~ngs...", CmdSettings, kbAltI,
                                    hcNoContext, "Alt-I" )
                  + newLine()
                  + *new TMenuItem( "~T~heme...", CmdTheme, kbAltT,
                                    hcNoContext, "Alt-T" ) ) )
        + newLine()
        + *new TMenuItem( "E~xit...",  cmQuit,  kbAltX, hcNoContext, "Alt-X" )
        + newLine()
        + *g_switchModelMenu_
        + *new TSubMenu( "~A~bout", kbAltA )
        + *new TMenuItem( "ab~out...", CmdHelp, kbAltA, hcNoContext, "Alt-A" )
        + newLine()
    );
}

TDeskTop *initDeskTop_( TRect r )
{
    TDeskTop *d = new TDeskTop( r );

    int x1 = r.a.x;
    int y1 = r.a.y;
    int x2 = r.b.x;
    int y2 = r.b.y;

    // Save the desktop rect so the transcript + prompt can be re-laid-out
    // together as the prompt grows/shrinks (relayoutPanels_).
    g_dX1 = x1; g_dY1 = y1; g_dX2 = x2; g_dY2 = y2;

    // Initial placeholder geometry; relayoutPanels_() refines it below so both
    // panels share the desktop exactly (no gap, no overlap). The scrollbar is
    // exactly ONE column wide: TScrollBar decides its orientation from its width
    // (size.x == 1 means vertical), so a wider box is drawn - and clicked - as a
    // horizontal bar. Its own drawing already includes both end arrows.
    TRect sbRect( x2 - 1, y1 + 1, x2, y2 - 1 );
    TRect trRect( x1, y1 + 1, x2 - 1, y2 - 1 );
    TRect inRect( x1, y2 - 2, x2, y2 - 1 );

    g_vBar_ = new TScrollBar( sbRect );
    g_vBar_->options &= ~ofSelectable;  // mouse-driven only; keep keyboard focus
                                        // on the prompt (see TranscriptView)
    d->insert( g_vBar_ );

    g_transcript_ = new TranscriptView( trRect, g_vBar_ );
    g_transcript_->hideCursor();
    g_transcript_->setThinkingExpandAll( atui::settings::thinkingExpandAll() );
    d->insert( g_transcript_ );

    // Inserted last, but it is the only selectable view in the desktop, so it
    // receives the desktop's initial keyboard focus.
    g_prompt_ = new PromptInput( inRect );
    d->insert( g_prompt_ );

    // Size the two panels to each other (prompt starts as separator + 1 row).
    relayoutPanels_();

    // The heartbeat, started in every configuration.  It arrived as "poll the token
    // queue", so it went inside the network block; everything the tick has grown since
    // -- noticing a stop request (SIGHUP, SIGTERM, Ctrl-C), the scripted exit,
    // recovering a file the last run left half written, keeping the two panels sized --
    // has nothing to do with having a server.  While it was gated, the no-transport
    // build ignored SIGTERM outright: nothing ever read the handler's flag, so the
    // program sat there until it was killed (measured: needed SIGKILL at 6s with the
    // timer inside the ifdef, stopped in 0.3s with it outside).  Fifty milliseconds of
    // a no-op pump is not a cost on any target this runs on.
    if( TProgram::application != 0 )
        TProgram::application->setTimer( 50, 50 );

#ifdef ATUI_HAVE_NETWORK
    // Periodic poll of the AI token queue (worker thread -> event loop).
    // And the permission agent: a second, cheap model that describes a command
    // the policy cannot settle by itself. It is only ever asked for a
    // description - tools/permissions.cpp decides - and with the mode set to
    // "agent" in a build that has no transport, nothing is installed here and
    // every undecided call is simply asked about.
    atui::tools::setPermissionJudge( &atui::net::oaiPermissionJudge() );
    updatePermissionStatus_();   // the label may now claim a model
#endif

    appendReply_( "atui (M2): type a message and press Enter." );
    updateProviderStatus_();   // status line shows "provider | model"
    focusInput_();
    return d;
}

// Lay out the transcript (top) and prompt (bottom) so they share the desktop
// exactly, with no gap and no overlap. The prompt is a separator row plus as
// many text rows as its current text needs (greedy-wrapped at the panel width),
// clamped so the transcript keeps at least a few rows. Call whenever the prompt
// text or the desktop size changes.
void relayoutPanels_()
{
    if( g_transcript_ == 0 || g_prompt_ == 0 ) return;

    // Sync with the real deskTop so a terminal resize is picked up the next
    // time we run (the periodic timer calls this even when idle).
    if( TProgram::deskTop )
    {
        const int ex = TProgram::deskTop->size.x, ey = TProgram::deskTop->size.y;
        if( ex != g_dX2 || ey != g_dY2 )
        {
            g_dX1 = 0; g_dY1 = 0; g_dX2 = ex; g_dY2 = ey;
        }
    }

    const int x1 = g_dX1, y1 = g_dY1, x2 = g_dX2, y2 = g_dY2;
    const int pwidth = x2 - 1 - x1;
    if( pwidth < 1 ) return;

    // How many text rows the prompt needs right now (separator excluded).
    int need = g_prompt_->rowCount( pwidth );
    if( need < 1 ) need = 1;

    const int bottom = y2 - 1;            // exclusive: last usable row is y2-2
    int avail = bottom - (y1 + 1);        // rows shared by transcript + prompt
    if( avail < 4 ) avail = 4;            // tiny terminal: give some room

    const int MIN_TR = 2;                 // keep some transcript visible
    int maxText = avail - 1 - MIN_TR;     // avail - separator - MIN_TR
    if( maxText < 1 ) maxText = 1;

    int textRows = need;
    if( textRows > maxText ) textRows = maxText;

    const int ptop = bottom - 1 - textRows;   // row 0 = separator, rows 1.. text

    const TRect pRect( x1, ptop, x2, bottom );
    const TRect tRect( x1, y1 + 1, x2 - 1, ptop );
    const TRect bRect( x2 - 1, y1 + 1, x2, ptop );   // one column, 1 == vertical

    const bool moved = !(g_prompt_->getBounds() == pRect);
    if( moved )
    {
        g_prompt_->setBounds( pRect );
        g_transcript_->setBounds( tRect );
        if( g_vBar_ )
        {
            g_vBar_->setBounds( bRect );
            g_vBar_->drawView();   // repaint reclaimed/vacated scrollbar columns
        }
    }

    // Always redraw the prompt (a few rows: cheap, and keeps the caret right).
    // Only touch the transcript when the split actually moved.
    if( moved ) g_transcript_->reflow();
    g_prompt_->drawView();
}

void focusInput_()
{
    if( g_prompt_ == 0 ) return;
    g_prompt_->clear();
    relayoutPanels_();
    g_prompt_->drawView();
    g_prompt_->focus();
}

void appendUser_( const char *text )
{
    String line;
    line += "> ";
    line += text;
    line += "\n\n";
    g_history_ += line.c_str();
    g_transcript_->appendUser( std::string( text ) );
}

void appendReply_( const char *text )
{
    String line;
    line += text;
    line += "\n\n";
    g_history_ += line.c_str();
    g_transcript_->appendAnswer( std::string( text ) );
    g_transcript_->finishReply();
}

#ifdef ATUI_HAVE_NETWORK
// Runs a turn until it is over, handing every record to the queue.
//
// This is the pump loop, and it lives here rather than in some generic runner because
// the decision it makes -- sleep, or step? -- belongs to whoever knows what the machine
// is waiting for.  `TurnQueue::waitIfWaiting()` is that knowledge expressed as data: the
// turn asked the user something, so sleeping is correct, and a thread burning a core
// beside an open dialog on a 30-year-old CPU would be the second worst thing it could
// do.  The worst would be spinning on the user's answer while the UI thread waits for a
// lock this one is holding.
//
// Nothing here touches the screen.  TVision belongs to the thread that created it, and
// that is not this one when there is a this one.
class TurnTask : public atui::core::Task
{
public:
    TurnTask() : turn_( 0 ), queue_( 0 ) {}

    void set( atui::net::ChatTurn &turn, atui::net::TurnQueue &queue,
              const std::string &message )
    {
        turn_ = &turn;
        queue_ = &queue;
        message_ = message;
    }

    virtual void run()
    {
        if( !turn_->start( message_, queue_ ) )
        {
            // Nothing was sent: say so, because the user already pressed Enter.
            // (Busy is not this: the caller refuses that before starting the thread.)
            queue_->onStatus( "the model was not asked (is a provider configured?)" );
            queue_->onTurnFinished( false );
            return;
        }
        // Run it to the end.  This is the whole reason for the thread: every wait in
        // here -- the DNS lookup, the first byte, a tool's child process -- is a wait
        // the screen does not have to sit through.
        while( turn_->busy() && !queue_->stopRequested() )
            driveOnce( kWorkerWaitMs );
    }

    // One pass of the pump: take any answer the UI left, then step or sleep.
    //
    // Split out so the build without threads can call it from the app timer instead of
    // running the loop above -- one body of logic either way, which is the entire point
    // of the arrangement, and the reason a `while` in `run()` cannot change behaviour
    // between the two platforms.
    void driveOnce( int waitMs )
    {
        bool allow = false, session = false;
        if( queue_->hasAnswer( allow, session ) )
        {
            turn_->answerApproval( allow, session );
            queue_->clearQuestion();
        }
        const bool waiting = queue_->waitIfWaiting();
        turnDebug_( "drive", turn_->busy() ? 1 : 0, waiting ? 1 : 0 );
        if( turn_->busy() && !waiting )
            turn_->step( waitMs );
        // Not `step( 50 )`, which is what this line used to say: the parameter arrived
        // here and was dropped on the floor, so the timer path -- the build with no
        // threads, where the comment above `drive()` explains at length why the wait
        // must be zero -- was waiting 50 ms inside select() on every tick, with a
        // keystroke queued behind it. Written as one body of logic for both callers,
        // ignored as one body of logic ignores it: a parameter that is not used is a
        // comment, and a comment that contradicts the code is worse than none.
    }

    // The timer's entry point, and the only one that checks the pointers: the turn may
    // not exist yet, and the timer runs from the first tick.
    //
    // Zero wait, and that number is not a detail: this is the build where this stack
    // pumps, so every millisecond spent inside `select` is a millisecond a keystroke
    // waits.  The worker has no such constraint, which is why it passes 50 -- a step
    // that waits is how you keep a thread from polling the network at full tilt.
    void drive()
    {
        if( turn_ == 0 || queue_ == 0 )
            return;
        driveOnce( 0 );
    }

    enum { kWorkerWaitMs = 50 };

private:
    atui::net::ChatTurn  *turn_;
    atui::net::TurnQueue *queue_;
    std::string           message_;
};

TurnTask g_task_;

// Where a record goes: the transcript, and only ever from the thread that owns it.
void showRecord_( const atui::net::TurnQueue::Record &rec )
{
    if( g_transcript_ == 0 )
        return;
    switch( rec.kind )
    {
        case atui::net::TurnQueue::tkText:
            g_transcript_->appendAnswer( rec.text );          break;
        case atui::net::TurnQueue::tkThinking:
            g_transcript_->appendThinking( rec.text );        break;
        case atui::net::TurnQueue::tkStatus:
            g_transcript_->appendStatus( rec.text );          break;
        case atui::net::TurnQueue::tkTool:
            g_transcript_->appendTool( rec.text );            break;
        case atui::net::TurnQueue::tkToolNote:
            g_transcript_->appendToolNote( rec.text );        break;
        case atui::net::TurnQueue::tkToolResult:
            g_transcript_->appendToolResult( rec.text );      break;
        case atui::net::TurnQueue::tkFinished:
            g_transcript_->finishReply();                     break;
    }
}

#endif // ATUI_HAVE_NETWORK

void pumpChat_()
{
#ifdef ATUI_HAVE_NETWORK
    turnDebug_( "pump", ( g_turn_ != 0 && g_turn_->busy() ) ? 1 : 0,
                g_transcript_ == 0 ? -1 : ( g_inStep_ ? 1 : 0 ) );
    if( g_transcript_ == 0 || g_inStep_ ) return;

    g_inStep_ = true;

    // Who steps the turn depends on the build; nothing below this line can tell, and
    // that is the design: a record that came from a worker's stack and one that came
    // from this stack are drawn by the same four lines.
    if( !g_turnThread_.running() )
    {
        // A few passes per tick rather than one that waits: an answer arrives in
        // pieces over seconds and the 50ms timer paces the redraw, so the pump takes
        // what is ready now and gives the keyboard back.  A long wait inside step()
        // would hold a keystroke for its length.
        for( int i = 0; i < 4; ++i )
        {
            if( g_turn_ == 0 || !g_turn_->busy() )
                break;
            g_task_.drive();
        }
    }

    // Records, oldest first.  The cap is per tick so that a long answer spreads over
    // ticks instead of holding the screen still for the whole of it; the next tick
    // takes the rest, and nothing is lost because the queue keeps them.
    //
    // Timed, and the whole pump below timed too, because a measured gap between tick
    // lines means a tick handler that took that long -- the tick line is written first --
    // and on the box a long streamed answer produced 5 second gaps with the input wait
    // ruled out (20 waits a second, none over 70ms: TESTING.md).  Which half of the pump
    // spends the time is the difference between a terminal that cannot drain and a queue
    // that is not being read.  Only a slow pass is worth a line: at 20 ticks a second an
    // unconditional duration log buries the events it was added to put in context.
    const unsigned long tPump0 = nowMs_();
    atui::net::TurnQueue::Record rec;
    long drawn = 0;
    g_transcript_->beginBatch();
    for( int i = 0; i < 64; ++i )
    {
        if( !g_queue_.pop( rec ) )
            break;
        showRecord_( rec );
        ++drawn;
    }
    g_transcript_->endBatch();
    const long drawMs = (long) ( nowMs_() - tPump0 );
    if( drawMs >= 100 )
        turnDebug_( "drawslow", drawMs, drawn );

    // The question, if the turn is waiting on one.  Modal -- and the answer goes into
    // the queue rather than into the turn, because the thread stepping that turn owns
    // it.  That is what wakes the worker; with no threads it is the next tick's
    // `drive()` that hands the answer over.
    atui::tools::ApprovalRef question;
    if( g_queue_.takeQuestion( question ) )
    {
        // Asked to stop, so do not ask: the answer to a permission question that is
        // not going to be answered is the safe one, and there is no point drawing a
        // box that the tick below is about to quit from under.
        const bool stopping = atui::core::shutdownRequested();
        const int answer = stopping ? apNo : showApprovalDialog_( question );
        g_queue_.answer( !stopping && answer != apNo,
                         !stopping && answer == apSession );
    }

    g_inStep_ = false;

    const long pumpMs = (long) ( nowMs_() - tPump0 );
    if( pumpMs >= 100 )
        turnDebug_( "pumpslow", pumpMs, drawMs );   // b: what the draw itself took
#endif // ATUI_HAVE_NETWORK
}

void processMessage_()
{
    if( g_prompt_ == 0 || g_transcript_ == 0 ) return;

    std::string text = g_prompt_->text();
    String msg( text.c_str() );
    if( msg.size() <= 0 ) return;

#ifdef ATUI_HAVE_NETWORK
    if( !g_turn_ )
        g_turn_ = new atui::net::ChatTurn();

    if( g_turnThread_.running() )
    {
        // Busy: a turn is in flight.  Refusing beats interleaving two conversations,
        // and the text stays in the prompt for the next try.
        g_transcript_->appendStatus( "(busy, try again)" );
        return;
    }

    // A turn reads its endpoint when it starts, and where the platform has threads it
    // starts on another stack -- so look at the settings here, on the thread that owns
    // the screen.  It fails now, with something the user can act on, instead of as a
    // record from a thread nobody expected; and the provider store, which is built on
    // first use, is not built by whichever thread got there first (PORTING.md, "What a
    // threaded host has to guard").
    if( atui::net::providerSettings().baseUrl.empty() )
    {
        g_transcript_->appendStatus( "(no provider configured - Alt-P)" );
        return;
    }

    // The turn carries the conversation itself now (only rounds the model actually
    // answered are in it), so the transcript text is no longer fed back as history.
    const std::string message( msg.c_str(), msg.size() );
    g_task_.set( *g_turn_, g_queue_, message );

    // Where the work runs is the platform's business; what runs is the same either
    // way.  With a thread, the turn leaves this stack now -- and the waits that have
    // no non-blocking form, the DNS lookup before the first byte and the child process
    // of a tool call, happen somewhere the screen does not depend on.  Without one,
    // `pumpChat_()` steps the same task from the timer.  A thread the OS refuses (out
    // of memory, out of handles) takes the same fallback rather than failing: the user
    // asked for an answer, not for a report about threads.
    std::string why;
    const bool threaded = atui::core::threadsAvailable()
                          && g_turnThread_.start( g_task_, &why );
    turnDebug_( "ask", (long) message.size(), threaded ? 1 : 0 );
    if( !threaded && !g_turn_->start( message, &g_queue_ ) )
    {
        g_transcript_->appendStatus( "(busy, try again)" );
        return;
    }

    appendUser_( msg.c_str() );
#else
    String reply = g_stub_->ask( msg, g_history_ );
    appendUser_( msg.c_str() );
    appendReply_( reply.c_str() );
#endif

    // Clear the prompt (collapses it back to a single row) for the next input.
    g_prompt_->clear();
    relayoutPanels_();
    g_prompt_->drawView();
}

void TApp::showHelp()
{
    TDialog *d = new TDialog( TRect( 12, 9, 72, 24 ), "atui - About" );
    // TStaticText wraps at the box width and then clips to its height, so every
    // line here is under the wrap column and the nine rows they make fit the box.
    // Lengthening this text means widening or growing the dialog, not adding a
    // line: a dropped sentence in an About box looks like a program with less to
    // say than it has.
    d->insert( new TStaticText( TRect( 3, 2, d->size.x - 2, d->size.y - 3 ),
        "atui - AI development TUI (M2)\n"
        "Streaming chat with tool calling.\n"
        "\n"
        "Switch model picks the provider; Alt-P edits them,\n"
        "Alt-T the theme, Ctrl-T a thinking block, and Alt-U\n"
        "lists the files atui changed so one can go back.\n"
        "\n"
        "Nothing outside this project is written unasked, and\n"
        "a command runs without atui's own credentials."
        ) );
    TButton *b = new TButton( TRect( 0, 0, 14, 3 ), "OK", cmCancel, bfNormal );
    d->insert( b );
    b->moveTo( (d->size.x - b->size.x) / 2, d->size.y - 3 );

#ifdef ATUI_DEBUG_KEYS
    g_keylog << "showHelp: size=(" << d->size.x << "," << d->size.y
             << ") btn y=" << b->origin.y << "\n";
    g_keylog.flush();
#endif
    deskTop->execView( d );
    destroy( d );
#ifdef ATUI_DEBUG_KEYS
    g_keylog << "showHelp: done\n";
    g_keylog.flush();
#endif
}

// Copy text into a TInputLine (fills its buffer to maxLen, as setData expects).
static void setInput_( TInputLine *line, const std::string &text )
{
    std::string rec( line->maxLen, '\0' );
    size_t n = text.size() < (size_t) line->maxLen ? text.size() : (size_t) line->maxLen;
    std::memcpy( &rec[0], text.data(), n );
    line->setData( &rec[0] );
}

// Duplicate a std::string into a fresh new char[] (TStringCollection::freeItem
// releases its items with delete[], so plain strdup would be a mismatch).
static char *dupStr_( const std::string &s )
{
    char *p = new char[ s.size() + 1 ];
    std::memcpy( p, s.data(), s.size() );
    p[ s.size() ] = '\0';
    return p;
}

// --- switch model -------------------------------------------------------------
//
// The "Switch model" menu-bar dropdown lists one entry per provider: the model
// that provider is configured to use. Picking an entry makes that provider
// active (its configured model comes with it), so switching is one click and
// the list stays short instead of showing every model a server happens to
// offer. Creating or editing providers stays under Options > Providers.
//
// TVision reads TMenuItem::subMenu each time a menu opens and ~TMenu() releases
// the old item chain, so refreshing means "build a new submenu, delete the
// previous one". Commands encode the index of the picked provider.

// Menu label text: drops TVision's '~' hot-key marker (a model id may contain
// one) and caps the length so the dropdown stays readable.
static std::string menuText_( const std::string &text, size_t maxLen = 52 )
{
    std::string out;
    for( size_t i = 0; i < text.size() && out.size() < maxLen; ++i )
        if( text[i] != '~' ) out += text[i];
    return out;
}

// Collects generated items into a chain and hands it to a TMenu (which then
// owns and deletes them).
class MenuBuilder
{
public:
    // In the constructor, not at the members: `= 0` on a member is C++11.
    MenuBuilder()
        : head_( 0 ), link_( &head_ )
    {
    }

    void add( TMenuItem *item ) { *link_ = item; link_ = &item->next; }
    void sep()                  { add( &newLine() ); }
    TMenu *finish()
    {
        TMenu *m = new TMenu();
        m->items = head_;
        m->deflt = head_;
        return m;
    }

private:
    TMenuItem  *head_;
    TMenuItem **link_;
};

// "model  (provider)" per provider, the active one marked with '*'. A provider
// with no model set shows its name, so an entry is never blank.
TMenu *buildSwitchModelMenu_()
{
    std::vector<atui::net::ProviderSettings> &all = atui::net::providers();
    const std::size_t active = atui::net::activeProviderIndex();

    MenuBuilder mb;
    for( std::size_t i = 0; i < all.size() && (int) i < kMaxSwitchModels; ++i )
    {
        const std::string model =
            all[i].model.empty() ? all[i].name : all[i].model;
        const std::string label =
            std::string( i == active ? "* " : "  " ) + menuText_( model ) +
            "  (" + menuText_( all[i].name, 20 ) + ")";
        // TKey( ... ) spelled out: kbNoKey is a constant whose value is 0, and in
        // C++98 that makes it a null pointer constant as well, which would leave
        // the TMenu* overload of this constructor looking like an equally good
        // answer.  (C++11 narrowed what counts, so newer compilers do not see the
        // second choice at all.)
        mb.add( new TMenuItem( label.c_str(),
                               (ushort) ( CmdSwitchModelBase + (int) i ),
                               TKey( kbNoKey ) ) );
    }
    return mb.finish();
}

static void setSubMenu_( TMenuItem *item, TMenu *menu )
{
    if( item == 0 ) return;
    TMenu *old = item->subMenu;
    item->subMenu = menu;
    delete old;
}

// Rebuilds the dropdown from the store. Called after any change to the store,
// from the event loop only (never while that menu is open).
void refreshSwitchModelMenu_()
{
    setSubMenu_( g_switchModelMenu_, buildSwitchModelMenu_() );
    if( TProgram::menuBar ) TProgram::menuBar->drawView();
}

// Status line: what a message will actually be sent to. TStatusLine lays its
// items out from their text on every draw, so rewriting the string and
// redrawing is enough.
void updateProviderStatus_()
{
    if( g_statusProvider_ == 0 ) return;
    const atui::net::ProviderSettings &p = atui::net::providerSettings();
    // The model id usually comes from the server's /models, and a server that writes
    // UTF-8 should not be able to put a broken glyph in the one place that is always
    // on screen.
    const std::string text = atui::tui::foldForTerminal(
        " " + menuText_( p.name, 16 ) + " | " + menuText_( p.model, 24 ) + " " );
    delete[] g_statusProvider_->text;
    g_statusProvider_->text = newStr( text.c_str() );
    if( TProgram::statusLine ) TProgram::statusLine->drawView();
}

// Status line: the permission mode, in words a user can act on. "agent" is only
// claimed when there is a model to ask - in a build without one, or before the
// agent is installed, the honest label is the one that describes what will
// actually happen, which is being asked.
void updatePermissionStatus_()
{
    if( g_statusPermission_ == 0 ) return;
    const atui::tools::PermissionMode mode = atui::tools::permissionMode();
    std::string text = " perm: asks first ";
    if( mode == atui::tools::pmExact )
        text = " perm: in-project ok, asks outside ";
    else if( mode == atui::tools::pmAgent )
        text = atui::tools::permissionJudge()
            ? " perm: in-project ok, a model checks the rest "
            : " perm: in-project ok, asks about commands ";
    delete[] g_statusPermission_->text;
    g_statusPermission_->text = newStr( text.c_str() );
    if( TProgram::statusLine ) TProgram::statusLine->drawView();
}

// "Switch model" entry: activate the picked provider; the model configured for
// it becomes the model used.
void pickSwitchModel_( int index )
{
    if( index < 0 || (std::size_t) index >= atui::net::providers().size() )
        return;
    atui::net::setActiveProvider( (std::size_t) index );
    atui::settings::saveAll();
    refreshSwitchModelMenu_();
    updateProviderStatus_();
    focusInput_();
}

// A list box that "selects" on a single mouse click. The stock TListBox only
// fires selectItem() on double-click / Space; here a single click picks the
// model immediately (the dialog reacts to the cmListItemSelected broadcast).
class ModelListBox : public TListBox
{
public:

    ModelListBox( const TRect& bounds, ushort aNumCols, TScrollBar *aScrollBar )
        : TListBox( bounds, aNumCols, aScrollBar ) {}

    virtual void handleEvent( TEvent& event )
    {
        bool press = ( event.what == evMouseDown ) &&
                     !( event.mouse.eventFlags & meDoubleClick );
        TListBox::handleEvent( event );   // moves focus to the clicked row
        // Rely on the viewer's own focus tracking (it maps the click to an
        // item); a single click selects immediately.
        if( press && focused >= 0 && focused < range )
            selectItem( focused );
    }
};

// Options > Providers dialog: manage the provider list (OpenAI-compatible
// endpoints). The list box on the left picks which provider the fields describe;
// "+ New" / "- Delete" / "Make active" and clicking a model act on the store
// immediately (and persist), while the fields themselves are written on OK.
//
// "Available models" is filled from the server's GET /models endpoint (a
// tvision TListBox + TScrollBar, the tvdemo fileview/listdlg pattern) and
// cached on the provider, so the box is not empty offline. Fetching needs
// the network build; otherwise the box stays empty and the status line says why.
enum { CmdProviderAdd = 2013, CmdProviderDel = 2014, CmdProviderActive = 2015 };

class ProvidersDialog : public TDialog
{
public:

    // Zeroed in the constructor: `= 0` on a member is C++11.  The fields a
    // dialog does not build stay null, and the code below tests for that.
    int            editing;         // provider the fields describe
    TInputLine    *name;
    TInputLine    *url;
    TInputLine    *key;
    TInputLine    *model;
    TInputLine    *temp;
    TCheckBoxes   *resend;
    TListBox      *providerList;
    TListBox      *modelList;
    TStaticText   *status;
    TRect          statusRect;

    ProvidersDialog();

    void loadProviderList();     // provider names, '*' marking the active one
    void loadFields();           // fields <- providers()[editing]
    void loadModelList();        // model box <- providers()[editing].models
    void refreshModels();        // GET /models with the URL/Key as typed
    void setStatus( const std::string &text );
    void commitFields();         // fields -> providers()[editing]

    virtual void handleEvent( TEvent& event );
};

// Reads one row of a list box into a string.
static std::string listRow_( TListBox *box, short row )
{
    char buf[ 160 ];
    buf[ 0 ] = '\0';
    if( box && row >= 0 )
    {
        box->getText( buf, row, sizeof buf - 1 );
        buf[ sizeof buf - 1 ] = '\0';
    }
    return std::string( buf );
}

// The store index a provider-list row stands for. TListBox items come back
// sorted (TStringCollection), so a row number is not a provider index: the row
// text (minus the active marker) is what identifies the entry.
static int providerRowToIndex_( TListBox *box, short row )
{
    std::string text = listRow_( box, row );
    while( !text.empty() && text[0] == ' ' ) text.erase( text.begin() );
    while( !text.empty() && ( text[text.size() - 1] == ' ' ||
                              text[text.size() - 1] == '*' ) )
        text.erase( text.end() - 1 );

    const std::vector<atui::net::ProviderSettings> &all = atui::net::providers();
    for( std::size_t i = 0; i < all.size(); ++i )
        if( all[i].name == text )
            return (int) i;
    return -1;
}

// The row of a list box holding 'text' (or -1), so a selection can be restored
// without assuming the items kept their insertion order.
static short listRowFor_( TListBox *box, const std::string &text )
{
    TCollection *items = box ? box->list() : 0;
    if( !items ) return -1;
    for( short i = 0; i < items->getCount(); ++i )
    {
        std::string row = listRow_( box, i );
        while( !row.empty() && row[0] == ' ' ) row.erase( row.begin() );
        while( !row.empty() && ( row[row.size() - 1] == ' ' ||
                                 row[row.size() - 1] == '*' ) )
            row.erase( row.end() - 1 );
        if( row == text ) return i;
    }
    return -1;
}

// The provider the fields currently describe (clamped to the store).
static std::size_t editingIndex_( const ProvidersDialog *d )
{
    const std::size_t n = atui::net::providers().size();
    if( d->editing < 0 || (std::size_t) d->editing >= n )
        return atui::net::activeProviderIndex();
    return (std::size_t) d->editing;
}

ProvidersDialog::ProvidersDialog()
    : TWindowInit( &TDialog::initFrame ),
      TDialog( TRect( 0, 0, 72, 25 ), "Options - Providers" ),
      editing( 0 ), name( 0 ), url( 0 ), key( 0 ), model( 0 ), temp( 0 ),
      resend( 0 ), providerList( 0 ), modelList( 0 ), status( 0 )
{
    options |= ofCentered;

    insert( new TStaticText( TRect( 3, 1, size.x - 3, 2 ),
        "Pick a provider or a model to switch it now; OK saves the fields." ) );

    // --- providers (click to edit one) -------------------------------------
    TScrollBar *psb = new TScrollBar( TRect( 30, 3, 31, 9 ) );
    insert( psb );
    providerList = new ModelListBox( TRect( 3, 3, 30, 9 ), 1, psb );
    providerList->newList( new TStringCollection( 4096, 32 ) );
    insert( providerList );

    insert( new TButton( TRect( 34, 3, 48, 5 ), "+ ~N~ew",
                         CmdProviderAdd, bfNormal ) );
    insert( new TButton( TRect( 50, 3, 64, 5 ), "- ~D~elete",
                         CmdProviderDel, bfNormal ) );
    insert( new TButton( TRect( 34, 6, 50, 8 ), "Make ~a~ctive",
                         CmdProviderActive, bfNormal ) );

    // --- fields of the provider being edited -------------------------------
    name  = new TInputLine( TRect( 10, 10, size.x - 3, 11 ), 64 );
    url   = new TInputLine( TRect( 10, 11, size.x - 3, 12 ), 128 );
    key   = new TInputLine( TRect( 10, 12, size.x - 3, 13 ), 64 );
    model = new TInputLine( TRect( 10, 13, 44, 14 ), 128 );
    temp  = new TInputLine( TRect( 51, 13, 58, 14 ), 8 );

    insert( new TLabel( TRect( 3, 10, 9, 11 ), "~N~ame", name ) );
    insert( name );
    insert( new TLabel( TRect( 3, 11, 9, 12 ), "~U~rl", url ) );
    insert( url );
    insert( new TLabel( TRect( 3, 12, 9, 13 ), "~K~ey", key ) );
    insert( key );
    insert( new TLabel( TRect( 3, 13, 9, 14 ), "~M~odel", model ) );
    insert( model );
    insert( new TLabel( TRect( 46, 13, 51, 14 ), "~T~emp", temp ) );
    insert( temp );

    resend = new TCheckBoxes( TRect( 3, 14, 44, 15 ),
        new TSItem( "~R~esend thinking in history", NULL ) );
    insert( resend );

    // --- models of the provider being edited -------------------------------
    insert( new TStaticText( TRect( 3, 16, 22, 17 ), "Available models" ) );
    insert( new TButton( TRect( size.x - 18, 15, size.x - 3, 17 ),
                         "~R~efresh", CmdRefreshModels, bfNormal ) );

    TScrollBar *sb = new TScrollBar( TRect( size.x - 4, 16, size.x - 3, 21 ) );
    insert( sb );
    modelList = new ModelListBox( TRect( 3, 16, size.x - 5, 21 ), 1, sb );
    modelList->newList( new TStringCollection( 4096, 32 ) );
    insert( modelList );

    statusRect = TRect( 3, 21, size.x - 3, 22 );
    setStatus( "" );

    TButton *ok     = new TButton( TRect( 20, 22, 34, 24 ), "OK",
                                   cmOK, bfDefault );
    TButton *cancel = new TButton( TRect( 36, 22, 50, 24 ), "Cancel",
                                   cmCancel, bfNormal );
    insert( ok );
    insert( cancel );
}

// Rebuilds the provider box: one row per provider, '*' marking the active one.
void ProvidersDialog::loadProviderList()
{
    std::vector<atui::net::ProviderSettings> &all = atui::net::providers();
    const std::size_t active = atui::net::activeProviderIndex();

    TStringCollection *coll = new TStringCollection( 4096, 32 );
    for( std::size_t i = 0; i < all.size(); ++i )
        coll->insert( dupStr_( all[i].name + ( i == active ? " *" : "" ) ) );
    providerList->newList( coll );

    if( !all.empty() )
    {
        const std::size_t keep = editingIndex_( this );
        editing = (int) keep;
        const short row = listRowFor_( providerList, all[ keep ].name );
        if( row >= 0 ) providerList->focusItem( row );
    }
}

void ProvidersDialog::loadFields()
{
    if( atui::net::providers().empty() ) return;
    editing = (int) editingIndex_( this );
    const atui::net::ProviderSettings &p = atui::net::providers()[ editing ];

    setInput_( name, p.name );
    setInput_( url, p.baseUrl );
    setInput_( key, p.apiKey );
    setInput_( model, p.model );
    char tbuf[ 16 ];
    std::snprintf( tbuf, sizeof tbuf, "%.2f", p.temperature );
    setInput_( temp, tbuf );
    ushort rinit = p.thinkingResend ? 1 : 0;
    resend->setData( &rinit );
    loadModelList();
}

void ProvidersDialog::loadModelList()
{
    TStringCollection *coll = new TStringCollection( 4096, 32 );
    std::string selected;
    if( !atui::net::providers().empty() )
    {
        const atui::net::ProviderSettings &p =
            atui::net::providers()[ editingIndex_( this ) ];
        selected = p.model;
        for( std::vector<std::string>::const_iterator it = p.models.begin();
             it != p.models.end(); ++it )
        {
            coll->insert( dupStr_( atui::tui::foldForTerminal( *it ) ) );
        }
    }
    modelList->newList( coll );
    const short row = listRowFor_( modelList, selected );
    if( row >= 0 ) modelList->focusItem( row );
}

void ProvidersDialog::setStatus( const std::string &text )
{
    if( status )
    {
        destroy( status );
        status = 0;
    }
    const std::string shown = atui::tui::foldForTerminal( text );
    status = new TStaticText( statusRect, shown.c_str() );
    insert( status );
    status->drawView();
}

// Writes the fields back to the provider being edited. Names stay unique (they
// double as the config section ids), and a hand-typed model id is added to the
// provider's list so it is offered again next time.
void ProvidersDialog::commitFields()
{
    if( atui::net::providers().empty() ) return;
    atui::net::ProviderSettings &p = atui::net::providers()[ editingIndex_( this ) ];

    const std::string n( name->data ), u( url->data ), k( key->data ),
                      m( model->data ), tv( temp->data );
    if( !n.empty() && n != p.name )
    {
        if( atui::net::findProvider( n ) ) p.name = atui::net::uniqueProviderName( n );
        else                               p.name = n;
    }
    if( !u.empty() ) p.baseUrl = u;
    p.apiKey = k;                       // clearing the key is allowed
    atui::net::selectModel( p, m );
    if( !tv.empty() )
    {
        double x = std::atof( tv.c_str() );
        if( x >= 0.0 ) p.temperature = x;
    }
    ushort rval = 0;
    resend->getData( &rval );
    p.thinkingResend = ( rval & 0x0001 ) != 0;
}

void ProvidersDialog::refreshModels()
{
    std::string u( url->data );
    std::string k( key->data );
    // Trim trailing whitespace/newline that can slip into the input buffer.
    // Index and erase, not back()/pop_back(): Watcom's std::string has neither
    // (measured; its member list stops at push_back).
    while( !u.empty() && ( u[u.size() - 1] == ' ' || u[u.size() - 1] == '\r' ||
                           u[u.size() - 1] == '\n' ) )
        u.erase( u.size() - 1 );

    std::vector<std::string> ids;
    std::string err;
    bool fetched = false;
#ifdef ATUI_HAVE_NETWORK
    fetched = atui::net::fetchModels( u, k, ids, err );
#endif

    if( !fetched )
    {
        modelList->newList( new TStringCollection( 4096, 32 ) );
#ifdef ATUI_HAVE_NETWORK
        setStatus( "Refresh failed: " + err );
#else
        (void) u; (void) k;
        setStatus( "Model list needs a network build." );
#endif
        return;
    }

    // Cache the list on the provider being edited (so it is available offline),
    // keeping whatever the Model field currently says.
    if( !atui::net::providers().empty() )
    {
        atui::net::ProviderSettings &p =
            atui::net::providers()[ editingIndex_( this ) ];
        const std::string want( model->data );
        if( !want.empty() ) p.model = want;   // keep what the field says
        atui::net::setModels( p, ids );
        setInput_( model, p.model );
        loadModelList();
    }
    // Built as text rather than with snprintf's %zu: that length modifier is a
    // C99-ism and Watcom's printf does not have it (it would print a count of
    // zero, which is a convincing wrong answer).
    setStatus( atui::core::toString( (unsigned long) ids.size() ) +
               " model(s) loaded." );
}

void ProvidersDialog::handleEvent( TEvent& event )
{
    if( event.what == evCommand )
    {
        switch( event.message.command )
        {
            case CmdRefreshModels:
                refreshModels();
                clearEvent( event );
                return;

            case CmdProviderAdd:
                commitFields();
                editing = (int) atui::net::addProvider( "provider" );
                atui::settings::saveAll();
                loadProviderList();
                loadFields();
                setStatus( "Created " + atui::net::providers()[ editing ].name +
                           ": set its URL and key." );
                clearEvent( event );
                return;

            case CmdProviderDel:
                if( !atui::net::removeProvider( editingIndex_( this ) ) )
                    setStatus( "Cannot delete the only provider." );
                else
                {
                    editing = (int) editingIndex_( this );
                    atui::settings::saveAll();
                    loadProviderList();
                    loadFields();
                    setStatus( "Provider deleted." );
                }
                clearEvent( event );
                return;

            case CmdProviderActive:
                commitFields();
                atui::net::setActiveProvider( editingIndex_( this ) );
                atui::settings::saveAll();
                loadProviderList();
                setStatus( "Active provider: " +
                           atui::net::providers()[ editingIndex_( this ) ].name );
                clearEvent( event );
                return;

            default:
                break;
        }
    }
    else if( event.what == evBroadcast &&
             event.message.command == cmListItemSelected )
    {
        if( event.message.infoPtr == (void *) providerList )
        {
            // Editing another provider: its stored values replace the fields.
            const int row = providerRowToIndex_( providerList,
                                                providerList->focused );
            if( row >= 0 )
            {
                commitFields();
                editing = row;
                loadFields();
                setStatus( "Editing " + atui::net::providers()[ row ].name );
            }
            clearEvent( event );
            return;
        }
        if( event.message.infoPtr == (void *) modelList )
        {
            const std::string picked = listRow_( modelList, modelList->focused );
            if( !picked.empty() )
            {
                setInput_( model, picked );
                if( !atui::net::providers().empty() )
                {
                    atui::net::selectModel(
                        atui::net::providers()[ editingIndex_( this ) ], picked );
                    atui::settings::saveAll();
                }
                setStatus( "Model: " + picked );
            }
            clearEvent( event );
            return;
        }
    }
    TDialog::handleEvent( event );
}

// Options > Providers: configure the remote endpoints (see ProvidersDialog).
void showProvidersDialog_()
{
    ProvidersDialog *d = new ProvidersDialog();
    d->editing = (int) atui::net::activeProviderIndex();
    d->loadProviderList();
    d->loadFields();
    if( atui::net::providerSettings().models.empty() ) d->refreshModels();

    if( TProgram::deskTop->execView( d ) == cmOK )
    {
        d->commitFields();
        atui::settings::saveAll();
    }
    TObject::destroy( d );
}

void TApp::showProviders()
{
    showProvidersDialog_();
    refreshSwitchModelMenu_();
    updateProviderStatus_();
    focusInput_();
}


void TApp::showTheme()
{
    // Radio-button picker (tvdemo mousedlg.cpp pattern). Index order matches
    // the TSItem list below: 0 = classic, 1 = dark.
    TDialog *d = new TDialog( TRect( 0, 0, 34, 11 ), "Options - Theme" );
    d->options |= ofCentered;

    d->insert( new TStaticText( TRect( 3, 1, d->size.x - 2, 2 ),
        "Color theme:" ) );

    TRadioButtons *pick = new TRadioButtons(
        TRect( 5, 3, d->size.x - 3, 5 ),
        new TSItem( "~C~lassic",
        new TSItem( "~D~ark", NULL ) ) );
    d->insert( pick );

    ushort sel = ( currentTheme() == ThemeClassic ) ? 0 : 1;
    pick->setData( &sel );

    TButton *ok     = new TButton( TRect( 5,  7, 15, 9 ), "O~K~",
                                   cmOK, bfDefault );
    TButton *cancel = new TButton( TRect( 17, 7, 27, 9 ), "Cancel",
                                   cmCancel, bfNormal );
    d->insert( ok );
    d->insert( cancel );

    if( deskTop->execView( d ) == cmOK )
    {
        ushort chosen = 0;
        pick->getData( &chosen );
        if( setTheme( chosen == 0 ? ThemeClassic : ThemeDark ) )
            setScreenMode( TScreen::screenMode );
        atui::settings::saveAll();
    }
    destroy( d );
}

// Options > Settings: the numeric preferences, each a plain number field.
//
// Typed rather than picked from a list: what counts as a comfortable wheel step,
// or a patient enough wait, depends on the mouse and the model, and neither fits
// a fixed set of choices. Each field prints the range it accepts, because there
// is nothing left to click for a hint.
class SettingsDialog : public TDialog
{
public:
    // Zeroed in the constructor: `= 0` on a member is C++11.
    TInputLine    *wheel;
    TInputLine    *timeout;
    TRadioButtons *modes;
    TInputLine    *judgeModel;
    TInputLine    *judgeSeconds;
    TInputLine    *cmdTimeout;

    SettingsDialog();

    // The number in `line`, or `keep` when it holds none: clearing a field is
    // not a change, and 0 is outside every range here.
    static int numberIn( TInputLine *line, int keep );

    // Same, for a number a 16-bit `int` cannot hold: the command timeout is
    // allowed up to a day's worth of seconds. Here 0 *is* a value, and it is the
    // only way to say "no limit"; it is the empty field that means "unchanged".
    static long longIn( TInputLine *line, long keep );
};

SettingsDialog::SettingsDialog()
    : TWindowInit( &TDialog::initFrame ),
      TDialog( TRect( 0, 0, 66, 25 ), "Options - Settings" ),
      wheel( 0 ), timeout( 0 ), modes( 0 ), judgeModel( 0 ), judgeSeconds( 0 ),
      cmdTimeout( 0 )
{
    options |= ofCentered;

    char range[ 32 ];

    // Digits a field can hold. A TInputLine's limit counts the terminating NUL
    // and the widget spends its first and last column on scroll arrows, so n
    // digits need a limit of n + 1 and a field n + 2 columns wide: ask for 4 in
    // a 5 column field and the fourth keystroke is silently thrown away. Five
    // covers the widest value here (1440 minutes) with a digit to spare.
    const int kDigits = 5;
    const short fieldLeft = 31, noteLeft = fieldLeft + kDigits + 3;

    insert( new TStaticText( TRect( 3, 2, 30, 3 ),
                             "Mouse wheel, lines/notch:" ) );
    wheel = new TInputLine( TRect( fieldLeft, 2,
                                   fieldLeft + kDigits + 2, 3 ), kDigits + 1 );
    insert( wheel );
    std::snprintf( range, sizeof range, "%d to %d",
              atui::settings::kMinWheelLines, atui::settings::kMaxWheelLines );
    insert( new TStaticText( TRect( noteLeft, 2, size.x - 3, 3 ), range ) );

    insert( new TStaticText( TRect( 3, 4, 30, 5 ),
                             "Request timeout, minutes:" ) );
    timeout = new TInputLine( TRect( fieldLeft, 4,
                                     fieldLeft + kDigits + 2, 5 ), kDigits + 1 );
    insert( timeout );
    std::snprintf( range, sizeof range, "%d to %d",
              atui::net::kMinRequestMinutes, atui::net::kMaxRequestMinutes );
    insert( new TStaticText( TRect( noteLeft, 4, size.x - 3, 5 ), range ) );

    // Permissions: an enum is meant for radio buttons - the three modes are
    // choices, not numbers, and the parenthesised name is what goes in the
    // config file and in permissions.log, so the wording here can be revised
    // without the user losing the thread.
    insert( new TStaticText( TRect( 3, 6, size.x - 3, 7 ),
                             "Before running something:" ) );
    // The third choice is a second model call, so it is only offered by a build
    // that can make one.  A dialog that listed it without a transport would be
    // advertising a way of deciding that never decides -- and the mode would then
    // be saved into the config file, where it outlives the argument for it.
    TSItem *agentItem = atui::tools::agentModeAvailable()
        ? new TSItem( "A model judges what the facts cannot settle (agent)",
                      NULL )
        : NULL;
    modes = new TRadioButtons(
        TRect( 5, 7, size.x - 4, 10 ),
        new TSItem( "Always ask before a tool runs (ask_all)",
        new TSItem( "In-project work is fine, ask outside (exact)",
                    agentItem ) ) );
    insert( modes );
    ushort chosen = ( atui::tools::permissionMode() ==
                      atui::tools::pmExact ) ? 1
                    : ( atui::tools::permissionMode() ==
                        atui::tools::pmAgent ) ? 2 : 0;
    modes->setData( &chosen );

    if( atui::tools::agentModeAvailable() )
    {
        insert( new TStaticText( TRect( 3, 11, 30, 12 ),
                                 "Judging model:" ) );
        judgeModel = new TInputLine( TRect( fieldLeft, 11,
                                            (short) ( fieldLeft + 26 ), 12 ), 40 );
        insert( judgeModel );

        insert( new TStaticText( TRect( 3, 12, 30, 13 ),
                                 "Judging seconds:" ) );
        judgeSeconds = new TInputLine( TRect( fieldLeft, 12,
                                              fieldLeft + 4, 13 ), 3 );
        insert( judgeSeconds );
        std::snprintf( range, sizeof range, "%d to %d",
                  atui::tools::kMinJudgeSeconds, atui::tools::kMaxJudgeSeconds );
        insert( new TStaticText( TRect( fieldLeft + 6, 12, size.x - 3, 13 ),
                                 range ) );
    }
    else
    {
        // Two fields that would configure a model this build cannot reach, and in
        // their place the reason.  Empty space would read as a bug, and a
        // greyed-out field as a promise.
        insert( new TStaticText( TRect( 3, 11, size.x - 3, 13 ),
            "Judging is done by a second model, which needs a\n"
            "build with a network transport. This one asks." ) );
    }

    // `[tools] command_timeout`, and deliberately the only one of the two command
    // settings that is here. `pass_env` - the names to hand a command anyway -
    // stays in the config file on purpose: it is the setting whose whole job is to
    // let one credential-shaped variable through, and a dialog that makes it as
    // easy to type as the wheel speed is a dialog that gets typed in.
    insert( new TStaticText( TRect( 3, 13, 30, 14 ),
                             "Command timeout, sec:" ) );
    cmdTimeout = new TInputLine( TRect( fieldLeft, 13,
                                        fieldLeft + kDigits + 2, 14 ), kDigits + 1 );
    insert( cmdTimeout );
    std::snprintf( range, sizeof range, "%ld to %ld, 0 = none",
              atui::tools::kMinCommandTimeout, atui::tools::kMaxCommandTimeout );
    insert( new TStaticText( TRect( noteLeft, 13, size.x - 3, 14 ), range ) );

    setInput_( wheel, atui::core::toString( atui::settings::wheelLines() ) );
    setInput_( timeout, atui::core::toString( atui::net::requestMinutes() ) );
    if( judgeModel )
        setInput_( judgeModel, atui::tools::judgeModelOverride() );
    if( judgeSeconds )
        setInput_( judgeSeconds, atui::core::toString( atui::tools::judgeSeconds() ) );
    setInput_( cmdTimeout, atui::core::toString( atui::tools::commandTimeoutSeconds() ) );

    // TStaticText wraps at the width it was given and then clips to the height it
    // was given, silently both times - so a paragraph is measured in rows of the
    // box, not in sentences. The About box lost two lines that way, which is why
    // every row here is counted against the box it sits in and the dialog is 25
    // rows tall because DOS text mode is, and 25 rows is the whole screen.
    // One line or the other about judging, and both are the same length so the
    // paragraph below stays the same shape on screen.
    const char *judgeHelp = atui::tools::agentModeAvailable()
        ? "Judging is by a second model; blank uses the chat model."
        : "Nothing is judged here: no model can be reached.";
    insert( new TStaticText( TRect( 3, 15, size.x - 3, 22 ),
        ( std::string(
        "One wheel notch scrolls the chat by that many lines; the\n"
        "request timeout is per request, so a tool round gets it\n"
        "again. A command is stopped at its timeout along with\n"
        "everything it started, and 0 there means no limit. A\n"
        "field with no number in it keeps the value it has.\n"
        "\n" ) + judgeHelp ).c_str() ) );

    TButton *ok     = new TButton( TRect( 19, 22, 31, 24 ), "~O~K",
                                   cmOK, bfDefault );
    TButton *cancel = new TButton( TRect( 33, 22, 47, 24 ), "Cancel",
                                   cmCancel, bfNormal );
    insert( ok );
    insert( cancel );
}

int SettingsDialog::numberIn( TInputLine *line, int keep )
{
    const std::string text( line->data );
    const size_t digits = text.find_first_of( "0123456789" );
    if( digits == std::string::npos ) return keep;
    return std::atoi( text.c_str() + digits );
}

long SettingsDialog::longIn( TInputLine *line, long keep )
{
    const std::string text( line->data );
    const size_t digits = text.find_first_of( "0123456789" );
    if( digits == std::string::npos ) return keep;
    return std::atol( text.c_str() + digits );
}

// Each field is clamped into the range it prints, so an entry of 99999 becomes
// the largest accepted value rather than silently meaning something else.
void TApp::showSettings()
{
    SettingsDialog *d = new SettingsDialog;
    if( deskTop->execView( d ) == cmOK )
    {
        atui::settings::wheelLines() = atui::settings::clampWheelLines(
            SettingsDialog::numberIn( d->wheel, atui::settings::wheelLines() ) );
        atui::net::requestMinutes() = atui::net::clampRequestMinutes(
            SettingsDialog::numberIn( d->timeout, atui::net::requestMinutes() ) );

        // Permissions. Changing the mode takes effect on the next tool call;
        // nothing cached has to be thrown away, because a judgement is keyed on
        // the model and the project as well as the command.
        ushort chosen = 0;
        d->modes->getData( &chosen );
        atui::tools::permissionMode() =
            ( chosen == 1 ) ? atui::tools::pmExact
                            : ( chosen == 2 ) ? atui::tools::pmAgent
                                              : atui::tools::pmAskAll;

        // The judging fields are absent in a build that cannot reach a model, and
        // absent fields change nothing -- in particular they do not clear a value
        // written by a build that can.
        if( d->judgeModel && d->judgeSeconds )
        {
            std::string model( d->judgeModel->data );
            const size_t from = model.find_first_not_of( " \t" );
            const size_t to   = model.find_last_not_of( " \t" );
            atui::tools::judgeModelOverride() =
                ( from == std::string::npos ) ? std::string()
                                              : model.substr( from, to - from + 1 );

            atui::tools::judgeSeconds() = atui::tools::clampJudgeSeconds(
                SettingsDialog::numberIn( d->judgeSeconds,
                                          atui::tools::judgeSeconds() ) );
        }

        // Unlike the other fields, 0 in this one is an answer: no limit. Only a
        // field with no digits in it at all means "leave it alone".
        atui::tools::commandTimeoutSeconds() = atui::tools::clampCommandTimeout(
            SettingsDialog::longIn( d->cmdTimeout,
                                    atui::tools::commandTimeoutSeconds() ) );
        updatePermissionStatus_();

        atui::settings::saveAll();
    }
    destroy( d );
}

// --- undo a file change --------------------------------------------------------
//
// core/journal.h keeps a copy of every file atui is about to change; this is the
// only way to look at them from inside the program. It is deliberately a plain
// list with two actions. An undo that tried to merge changes, or that picked on
// its own which of several edits to roll back, would be a worse thing to trust
// with a file the user is working on than nine rows and a "Restore" button.
//
// What is listed is exactly what the journal directory contains, so the box and
// `type U000123.INF` on a machine with no tooling say the same thing.
//
// Commands of the undo dialog's own: a subclassed TDialog that intercepts them
// before TDialog sees them, which is how the provider dialog's buttons work.
enum { CmdUndoRestore = 2020, CmdUndoDiscard = 2021 };

// Nine rows is the most that fits a 25-row terminal with the header, the status
// line and the buttons, and single-digit numbering sorts correctly in the list
// box's collection (which sorts its items, so row order is label order).
const int kUndoRows = 9;

// "2026-10-13T09:14:02Z" -> "10-13 09:14". The year is left out of a list where
// every entry is from this one; the dialogs spell it out in full.
static std::string undoClock_( const std::string &stamp )
{
    if( stamp.size() < 16 ) return stamp;
    return stamp.substr( 5, 5 ) + " " + stamp.substr( 11, 5 );
}

// The target as the user would name it: relative to the project when it is in
// the project, absolute otherwise (a path that left the tree should not be
// printed as if it had not).
static std::string undoPath_( const std::string &target )
{
    const std::string root = atui::tools::projectRoot();
    if( !root.empty() && atui::core::startsWith( target, root.c_str() ) )
    {
        std::string rest = target.substr( root.size() );
        if( rest.size() > 1 && ( rest[0] == '/' || rest[0] == '\\' ) )
            return rest.substr( 1 );
        if( rest.size() == 1 && ( rest[0] == '/' || rest[0] == '\\' ) )
            return std::string( "." );
    }
    return target;
}

static std::string undoRowLabel_( const atui::core::journal::Entry &e, int ordinal )
{
    atui::core::Fmt s;
    s << ( ordinal + 1 ) << ". " << undoPath_( e.target )
      << "   " << undoClock_( e.stamp )
      << "  " << atui::core::journal::humanBytes( e.bytes );
    // An interrupted write goes first and says so: it is the entry that may be
    // the only copy of a file, and a list that buried it under newer edits would
    // be a list that hid the one thing worth acting on.
    if( e.pending ) s << "  INTERRUPTED";
    return s.str();
}

// A centred modal box with wrapped text and one to three buttons.
//
// Not stock messageBox(): that is sized for about three lines and clips the
// rest, which here would cut off the path or the rule the box exists to show.
// Buttons are given in display order, laid out from the right, and the LAST one
// is the default - TVision focuses the view inserted last, and a focused
// TButton makes itself the default, so the declining button goes last and Enter
// and Esc both do the safe thing.
ushort showModalBox_( const std::string &title, const std::string &body,
                      const char *labelA, ushort cmdA,
                      const char *labelB, ushort cmdB,
                      const char *labelC, ushort cmdC )
{
    struct Btn { const char *label; ushort cmd; };
    const Btn in[3] = { { labelA, cmdA }, { labelB, cmdB }, { labelC, cmdC } };
    int n = 0;
    for( int i = 0; i < 3; ++i )
        if( in[i].label ) n = i + 1;
    if( n == 0 ) return cmCancel;

    const short deskW = TProgram::deskTop ? TProgram::deskTop->size.x : 80;
    const short deskH = TProgram::deskTop ? TProgram::deskTop->size.y : 25;
    short textW = 60;
    if( textW > deskW - 10 ) textW = (short) ( deskW - 10 );
    if( textW < 24 ) textW = 24;

    std::vector<std::string> rows = wrapText_( body, textW );
    short maxRows = (short) ( deskH - 8 );
    if( maxRows < 1 ) maxRows = 1;
    if( (short) rows.size() > maxRows )
    {
        rows.resize( maxRows );
        rows.back() += "...";
    }

    std::string text;
    for( size_t i = 0; i < rows.size(); ++i )
    {
        if( i != 0 ) text += "\n";
        text += rows[i];
    }

    const short w = (short) ( textW + 6 );
    const short h = (short) ( (short) rows.size() + 5 );
    TDialog *d = new TDialog( TRect( 0, 0, w, h ), title.c_str() );
    d->options |= ofCentered;
    d->insert( new TStaticText( TRect( 3, 1, (short) ( w - 2 ), (short) ( h - 4 ) ),
                                text.c_str() ) );

    const short group = (short) ( n * 12 + ( n - 1 ) );
    short x = (short) ( w - 4 - group );
    if( x < 2 ) x = 2;
    for( int i = 0; i < n; ++i )
    {
        const bool last = ( i == n - 1 );
        d->insert( new TButton( TRect( x, (short) ( h - 3 ), (short) ( x + 12 ),
                                       (short) ( h - 1 ) ),
                                in[i].label, in[i].cmd,
                                last ? bfDefault : bfNormal ) );
        x = (short) ( x + 13 );
    }

    const ushort r = TProgram::deskTop
        ? TProgram::deskTop->execView( d )
        : TProgram::application->execView( d );
    TObject::destroy( d );
    return r;
}

// A question about a file, with No as the default.
static bool askYesNo_( const std::string &title, const std::string &body )
{
    return showModalBox_( title, body, "~Y~es", cmYes, "~N~o", cmNo, 0, 0 ) == cmYes;
}

// Something that happened, that the user should see before continuing.
static void showNote_( const std::string &title, const std::string &body )
{
    showModalBox_( title, body, "OK", cmOK, 0, 0, 0, 0 );
}

// Puts one entry's contents back.
//
// `consented` says a person has already been shown this exact file and said to
// restore it (the start-up recovery dialog). Otherwise the write goes through
// the permission policy like every other write atui makes, so a rule that will
// not let atui touch .env will not let it restore .env either - the fact that a
// menu item was clicked is not a fact about the file.
//
// One exception is made, and it is rung 16: a restore is refused for want of an
// undo copy when the copy in hand IS the undo copy, so that answer is dropped
// rather than repeated back at the user.
enum RestoreResult { urDone = 0, urDeclined, urFailed };

RestoreResult restoreEntry_( const atui::core::journal::Entry &entry,
                             bool consented, std::string &message )
{
    if( !consented )
    {
        const atui::tools::Permission p = atui::tools::decide(
            atui::tools::PermissionSubject( atui::tools::psPath, entry.target ) );
        if( p.outcome != atui::tools::poRun && p.rung != atui::tools::prNoUndoNet )
        {
            atui::core::Fmt q;
            q << "Restore '" << entry.target << "'?\n\n"
              << "It held these bytes on " << entry.stamp << ", and atui's rules "
              << "would not overwrite the file without asking a person:\n\n  "
              << atui::tools::decisionNote( p )
              << "\n\nPut the saved copy back?";
            if( !askYesNo_( "Restore file", q.str() ) )
            {
                message = "you said no";
                return urDeclined;
            }
        }
    }

    std::string err;
    if( !atui::core::journal::restore( entry, err ) )
    {
        message = err;
        return urFailed;
    }
    message = "restored " + undoPath_( entry.target );
    return urDone;
}

class UndoDialog : public TDialog
{
public:
    // Zeroed in the constructor: `= 0` on a member is C++11.
    TListBox      *list;
    TStaticText   *header;
    TStaticText   *status;
    TRect          headerRect;
    TRect          statusRect;
    std::vector<atui::core::journal::Entry> items;

    UndoDialog();
    void loadList();
    void setHeader( const std::string &text );
    void setStatus( const std::string &text );
    void act( bool discardIt );
    virtual void handleEvent( TEvent& event );
};

UndoDialog::UndoDialog()
    : TWindowInit( &TDialog::initFrame ),
      TDialog( TRect( 0, 0, 74, kUndoRows + 10 ), "Undo a file change" ),
      list( 0 ), header( 0 ), status( 0 )
{
    options |= ofCentered;

    // Two rows: the header names the journal directory, and directory paths are
    // longer than one row. TStaticText clips what does not fit rather than
    // wrapping, so the space has to be given up front.
    headerRect  = TRect( 3, 1, size.x - 3, 3 );
    const short listTop = 4;

    TScrollBar *sb = new TScrollBar( TRect( size.x - 4, listTop, size.x - 3,
                                            (short) ( listTop + kUndoRows - 1 ) ) );
    insert( sb );
    // Single click picks a row (the same ModelListBox the provider dialog uses):
    // the stock list box wants a double click, which is a poor thing to make
    // somebody do to a file they are about to overwrite.
    list = new ModelListBox( TRect( 3, listTop, (short) ( size.x - 5 ),
                                    (short) ( listTop + kUndoRows - 1 ) ), 1, sb );
    list->newList( new TStringCollection( 4096, 32 ) );
    insert( list );

    statusRect = TRect( 3, (short) ( listTop + kUndoRows ), size.x - 3,
                        (short) ( listTop + kUndoRows + 1 ) );
    setStatus( "Restore puts a saved copy back; Discard throws it away." );

    insert( new TButton( TRect( 3, size.y - 4, 17, size.y - 2 ),
                         "~R~estore", CmdUndoRestore, bfNormal ) );
    insert( new TButton( TRect( 19, size.y - 4, 33, size.y - 2 ),
                         "~D~iscard", CmdUndoDiscard, bfNormal ) );
    insert( new TButton( TRect( size.x - 16, size.y - 4, size.x - 3, size.y - 2 ),
                         "Cancel", cmCancel, bfDefault ) );

    loadList();
}

// Rebuilds the rows. Interrupted writes lead, then everything else newest first
// (which is what the journal already gives), capped at what fits on screen.
void UndoDialog::loadList()
{
    items.clear();
    atui::core::journal::entries( items );

    std::vector<atui::core::journal::Entry> ordered;
    for( std::size_t i = 0; i < items.size(); ++i )
        if( items[i].pending ) ordered.push_back( items[i] );
    for( std::size_t i = 0; i < items.size(); ++i )
        if( !items[i].pending ) ordered.push_back( items[i] );
    items.swap( ordered );

    TStringCollection *coll = new TStringCollection( 4096, 32 );
    const std::size_t shown = items.size() < (std::size_t) kUndoRows
                            ? items.size() : (std::size_t) kUndoRows;
    for( std::size_t i = 0; i < shown; ++i )
        coll->insert( dupStr_( undoRowLabel_( items[i], (int) i ) ) );
    list->newList( coll );

    atui::core::Fmt h;
    if( !atui::core::journal::enabled() )
        h << "The undo journal is switched off ([undo] enabled), so atui holds "
             "no copies of anything.";
    else
    {
        h << atui::core::journal::describe();
        // Nothing is hidden by this box, only fitted to it: the older entries are
        // still in the journal, and the header names the directory so they can be
        // read with any pager.
        if( shown < items.size() ) h << " - showing the newest " << shown;
    }
    setHeader( h.str() );
}

void UndoDialog::setHeader( const std::string &text )
{
    if( header ) { destroy( header ); header = 0; }
    header = new TStaticText( headerRect, text.c_str() );
    insert( header );
    header->drawView();
}

void UndoDialog::setStatus( const std::string &text )
{
    if( status ) { destroy( status ); status = 0; }
    status = new TStaticText( statusRect, text.c_str() );
    insert( status );
    status->drawView();
}

// Acts on the selected row. The row's leading number is what identifies the
// entry - the collection sorts its items, so a row position is not an index into
// `items`, and re-reading the label is how the provider dialog maps them back.
void UndoDialog::act( bool discardIt )
{
    // `focused` is the row the list box is on: TListViewer has no accessor, and
    // the ModelListBox above relies on the same member.
    const std::string row = listRow_( list, list->focused );
    if( row.empty() )
    {
        setStatus( "Pick a row first." );
        return;
    }
    size_t pos = 0;
    while( pos < row.size() && row[pos] >= '0' && row[pos] <= '9' ) ++pos;
    if( pos == 0 )
    {
        setStatus( "Pick a row first." );
        return;
    }
    const int which = std::atoi( row.substr( 0, pos ).c_str() ) - 1;
    if( which < 0 || (std::size_t) which >= items.size() )
    {
        setStatus( "That row is out of date; the list has been refreshed." );
        loadList();
        return;
    }

    const atui::core::journal::Entry entry = items[which];

    if( discardIt )
    {
        // Discarding is the one action here that cannot be undone: it destroys
        // the only copy of what the file used to hold.
        atui::core::Fmt q;
        q << "Throw away the saved copy of\n\n  " << undoPath_( entry.target )
          << "\n\nfrom " << entry.stamp << "? Nothing else holds what was in that "
          "file before atui changed it.";
        if( !askYesNo_( "Discard saved copy", q.str() ) ) return;

        std::string err;
        if( !atui::core::journal::discard( entry, err ) )
            setStatus( "Not discarded: " + err );
        else
            setStatus( "Discarded the copy of " + undoPath_( entry.target ) );
        loadList();
        return;
    }

    std::string message;
    const RestoreResult r = restoreEntry_( entry, false, message );
    // The transcript records what happened to a file, not what didn't: a declination
    // is already on screen in the dialog.
    if( g_transcript_ && r != urDeclined ) g_transcript_->appendStatus( message );
    setStatus( r == urDone ? message : "Not restored: " + message );
    loadList();
}

void UndoDialog::handleEvent( TEvent& event )
{
    if( event.what == evCommand )
    {
        if( event.message.command == CmdUndoRestore )
        {
            act( false );
            clearEvent( event );
            return;
        }
        if( event.message.command == CmdUndoDiscard )
        {
            act( true );
            clearEvent( event );
            return;
        }
    }
    TDialog::handleEvent( event );   // Cancel closes the box
}

void showUndoDialog_()
{
    UndoDialog *d = new UndoDialog();
    TProgram::deskTop ? TProgram::deskTop->execView( d )
                      : TProgram::application->execView( d );
    TObject::destroy( d );
    focusInput_();
}

// Once per run, deal with writes the last run did not finish.
//
// Called from the periodic timer rather than from the desktop's construction: a
// modal box needs a desktop to be modal on, and during initDeskTop() the one
// TVision is building is not yet TProgram::deskTop, which is what makes a dialog
// clickable.
void maybeRecoverInterruptedWrites_()
{
    static bool done = false;
    if( done ) return;
    done = true;

    if( !atui::core::journal::enabled() ) return;

    std::vector<atui::core::journal::Entry> broken;
    atui::core::journal::interrupted( broken );
    if( broken.empty() ) return;

    // First clear the entries that turned out to be paperwork: the file still
    // holds the bytes the copy holds, so the write never happened and there is
    // nothing to recover. Asking about those would teach the user to say no.
    std::vector<atui::core::journal::Entry> real;
    for( std::size_t i = 0; i < broken.size(); ++i )
    {
        std::string err;
        if( atui::core::journal::compareTarget( broken[i] ).empty() )
        {
            atui::core::journal::discard( broken[i], err );
            if( g_transcript_ )
            {
                g_transcript_->appendStatus(
                    "undo journal: an interrupted write had never reached "
                    + undoPath_( broken[i].target ) + "; nothing to recover" );
            }
        }
        else
            real.push_back( broken[i] );
    }

    // One dialog each. These are rare - a copy exists per interrupted write, and
    // a run that was killed mid-edit is not a normal event - and each one is
    // about a different file, which a single summary box cannot act on.
    int asked = 0;
    for( std::size_t i = 0; i < real.size(); ++i )
    {
        if( asked >= 3 )
        {
            if( g_transcript_ )
            {
                atui::core::Fmt s;
                s << ( real.size() - i )
                  << " more interrupted write(s) are in the undo journal ("
                  << atui::core::journal::dir() << "); File > Undo lists them.";
                g_transcript_->appendStatus( s.str() );
            }
            break;
        }
        ++asked;

        const atui::core::journal::Entry &e = real[i];
        atui::core::Fmt body;
        body << "atui stopped while it was writing\n\n  " << e.target << "\n\n"
             << "That file " << atui::core::journal::compareTarget( e )
             << ". The copy from before the write is still saved, at\n\n  "
             << e.preImage << "\n\n("
             << atui::core::journal::humanBytes( e.bytes ) << ", " << e.stamp
             << ")\n\nPut the saved copy back over the file?";

        // Default No: the file on disk might be good, and the copy might be
        // worse. Nothing is lost by answering no - the entry stays in the
        // journal and File > Undo still offers it.
        if( !askYesNo_( "Interrupted write", body.str() ) )
        {
            if( g_transcript_ )
            {
                g_transcript_->appendStatus( "left " + undoPath_( e.target )
                    + " as it is; the saved copy is still in the journal" );
            }
            continue;
        }

        // `consented` is true here: the person has just been shown this file, by
        // name, and answered yes about putting it back.
        std::string message;
        const RestoreResult r = restoreEntry_( e, true, message );
        if( g_transcript_ )
        {
            g_transcript_->appendStatus( r == urDone ? message
                                                    : "not restored: " + message );
        }
        // A restore that failed is worth a box of its own: the transcript line
        // scrolls, and "the copy is still at <path>" is the sentence that keeps
        // the user from thinking the file is gone.
        if( r == urFailed ) showNote_( "Could not restore", message );
    }
}

// Declared in app.h; writes the anonymous-namespace global above, which is visible by
// name from here because an unnamed namespace is used by its enclosing scope in the
// translation unit that declares it.
void setStartupMessage( const std::string &text )
{
    g_startupMessage = text;
}

int run_app()
{
    // Apply persisted settings (theme + provider) before the UI is built,
    // so the palette and endpoint reflect the saved config.
    atui::settings::loadAll();

    // Before the TApp, on purpose: tvision saves the dispositions in force when it
    // takes the console over and calls them from its own handler, so installing here
    // means a stop restores the screen AND sets the flag.  Installed later it would
    // replace tvision's handler, and the terminal would go out wrong.
    atui::core::installShutdownHandlers();

    // Read once, here, rather than from the tick: it never changes, and the tick has
    // better things to do than parse a string fifty times a second.
    g_quitAfterMs_ = quitAfterMs_();

    TApp app;

    // The desk is built by the constructor above (TProgInit runs the init handlers),
    // so the prompt is real here. Asking now costs nothing the loop has to wait for:
    // `processMessage_()` starts the turn and returns, and the turn is stepped by the
    // timer that starts with `run()`.
    if( !g_startupMessage.empty() )
    {
        if( g_prompt_ != 0 )
        {
            g_prompt_->setText( g_startupMessage );
            g_prompt_->drawView();
            processMessage_();
        }
        g_startupMessage.clear();
    }

    app.run();
    return 0;
}

} // namespace atui::tui
} // namespace atui
