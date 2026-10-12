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
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include <tvision/tv.h>

#include "app.h"
#include "core/ai.h"
#include "tui/theme.h"
#include "tui/md_style.h"
#include "tui/markdown.h"
#include "tui/thinking.h"
#include "tui/wrap.h"
#include "net/provider.h"
#include "settings/settings.h"
#include "tools/permissions.h"

#ifdef ATUI_HAVE_NETWORK
#include "net/oai_chat_model.h"
#include "net/oai_client.h"
#include "net/permission_judge.h"
#include "net/token_queue.h"
#include "tools/approval.h"
#include <mutex>
#endif

#include <vector>

namespace atui {
namespace tui {

using atui::core::String;

// Command ids. cm* are TVision's, Cmd* are ours. The "Switch model" dropdown
// uses a range: the command encodes the index of the picked provider, whose
// configured model comes with it.
enum { CmdHelp = 1000, CmdTheme = 1004, CmdProviders = 1003,
       CmdSettings = 1005,
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
    String g_history_;

    // Desktop rect (saved so the transcript + prompt can reflow together).
    int g_dX1 = 0, g_dY1 = 0, g_dX2 = 0, g_dY2 = 0;

    // Provider/model pickers: the "Switch model" menu-bar dropdown (rebuilt
    // from the provider store) and the status-line item showing the active
    // "provider | model".
    TSubMenu *g_switchModelMenu_ = 0;
    TStatusItem *g_statusProvider_ = 0;
    TStatusItem *g_statusPermission_ = 0;

    // M2 streaming state (only used when ATUI_HAVE_NETWORK).
#ifdef ATUI_HAVE_NETWORK
    atui::net::TokenQueue g_queue_;
    atui::net::OaiChatModel *g_oai_;
    bool g_streamActive_ = false;
#endif
    bool g_replyFinished_ = false;

    // Debug event log (only when built with ATUI_DEBUG_KEYS).
#ifdef ATUI_DEBUG_KEYS
    std::ofstream g_keylog;
#endif
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
void pollSink_();
TMenu *buildSwitchModelMenu_();
void refreshSwitchModelMenu_();
void updateProviderStatus_();
void updatePermissionStatus_();
void showProvidersDialog_();
void pickSwitchModel_( int index );

#ifdef ATUI_HAVE_NETWORK
// --- tool approval (worker thread -> UI) -------------------------------------
//
// A tool that requires approval (edit_file / run_command) blocks the request
// worker inside TuiApprover::approve() until this UI thread shows a Yes/No
// dialog. The worker publishes the pending request here; pollSink_() picks it
// up on its periodic timer, shows the modal box, and records the decision.

std::mutex                 g_apMx;               // guards g_pendingApproval
atui::tools::ApprovalRef   g_pendingApproval;    // worker sets, UI clears
bool                       g_approvalShowing = false;  // re-entrancy guard

// What the user made of a confirmation request. The third answer is not "allow
// this kind of thing": it is this call, tool and arguments included, for the
// rest of the run - the same key the plan cache uses, so a build loop that runs
// `make test` forty times asks once and not forty times.
enum ApprovalAnswer { apNo = 0, apOnce = 1, apSession = 2 };

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

    const short w = (short) ( textW + 6 );
    const short h = (short) ( (short) rows.size() + 5 );
    const std::string title = "Confirm - " + req->tool;
    TDialog *d = new TDialog( TRect( 0, 0, w, h ), title.c_str() );
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

// Approver installed process-wide. approve() runs on the worker thread.
class TuiApprover : public atui::tools::Approver
{
public:
    virtual bool approve( const atui::tools::ApprovalRef &req )
    {
        {
            std::lock_guard< std::mutex > lock( g_apMx );
            g_pendingApproval = req;
        }
        // Refuse rather than hang forever if the UI can't show the prompt.
        return req->waitFor( 600000 );   // 10 minutes
    }
};

TuiApprover g_approver;
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
    }

    // --- document building (called on the UI thread) ---------------------

    void appendUser( const std::string &text )
    {
        closeOpen();
        Block b; b.kind = kUser; b.text = text; b.open = false; b.expanded = false;
        blocks_.push_back( b );
        follow_ = true;
        relayout();
    }

    void appendStatus( const std::string &text )
    {
        closeOpen();
        Block b; b.kind = kStatus; b.text = text; b.open = false; b.expanded = false;
        blocks_.push_back( b );
        relayout();
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
        relayout();
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
        relayout();
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
        relayout();
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
        relayout();
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
        relayout();
    }

    void finishReply()
    {
        closeOpen();
        relayout();
    }

    // Toggle every thinking block (Ctrl-T).
    void toggleThinkingAll()
    {
        expandAll_ = !expandAll_;
        for( size_t i = 0; i < blocks_.size(); ++i )
            if( blocks_[i].kind == kThinking )
                blocks_[i].expanded = expandAll_;
        relayout();
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
                    relayout();
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
        const bool dark = ( currentTheme() == ThemeDark );
        const int bg = 0x152238;   // dark navy background
        const int cbg = 0x0C1626;  // code background (slightly darker)
        if( dark )
        {
            switch( index )
            {
                // transcript blocks
                case rUser:      return TColorAttr( TColor( 0x9BD49E ), TColor( bg ) );
                case rThinking:  return TColorAttr( TColor( 0x8A94A0 ), TColor( bg ) );
                case rHead:      return TColorAttr( TColor( 0x74CFCF ), TColor( bg ) );
                case rStatus:    return TColorAttr( TColor( 0xE9CB94 ), TColor( bg ) );
                case rTool:      return TColorAttr( TColor( 0x9CDCF0 ), TColor( bg ),
                                                    slBold );
                case rToolOut:   return TColorAttr( TColor( 0xB6C2CF ), TColor( bg ) );
                // markdown inline
                case rHeading:   return TColorAttr( TColor( 0xF7D488 ), TColor( bg ),
                                                    slBold );
                case rBold:      return TColorAttr( TColor( 0xF5EDCB ), TColor( bg ),
                                                    slBold );
                case rItalic:    return TColorAttr( TColor( 0xCFE3B8 ), TColor( bg ),
                                                    slItalic );
                case rBoldItalic:return TColorAttr( TColor( 0xF5EDCB ), TColor( bg ),
                                                    slBold | slItalic );
                case rCodeInline:return TColorAttr( TColor( 0xE8BFA0 ), TColor( cbg ) );
                case rLink:      return TColorAttr( TColor( 0x7FB6FF ), TColor( bg ),
                                                    slUnderline );
                case rQuote:     return TColorAttr( TColor( 0xA9B2BC ), TColor( bg ) );
                case rMarker:    return TColorAttr( TColor( 0x74CFCF ), TColor( bg ) );
                case rStrike:    return TColorAttr( TColor( 0x7E8794 ), TColor( bg ) );
                case rDim:       return TColorAttr( TColor( 0x8A94A0 ), TColor( bg ) );
                // code + syntax (all on the code background)
                case rCode:      return TColorAttr( TColor( 0xE6E1CE ), TColor( cbg ) );
                case rKeyword:   return TColorAttr( TColor( 0xF08B9A ), TColor( cbg ) );
                case rType:      return TColorAttr( TColor( 0x7FD1A6 ), TColor( cbg ) );
                case rString:    return TColorAttr( TColor( 0xC9C17A ), TColor( cbg ) );
                case rChar:      return TColorAttr( TColor( 0xC9C17A ), TColor( cbg ) );
                case rComment:   return TColorAttr( TColor( 0x6E7683 ), TColor( cbg ),
                                                    slItalic );
                case rNumber:    return TColorAttr( TColor( 0xE0A86B ), TColor( cbg ) );
                case rFunction:  return TColorAttr( TColor( 0x87B0F0 ), TColor( cbg ) );
                case rPreproc:   return TColorAttr( TColor( 0xB88AD0 ), TColor( cbg ) );
                case rOperator:  return TColorAttr( TColor( 0xC8C4B6 ), TColor( cbg ) );
                case rNormal:
                default:         return TColorAttr( TColor( 0xF2E4AC ), TColor( bg ) );
            }
        }
        switch( index )
        {
            case rUser:      return TColorAttr( 0x1A );
            case rThinking:  return TColorAttr( 0x17 );
            case rHead:      return TColorAttr( 0x13 );
            case rStatus:    return TColorAttr( 0x1D );
            case rTool:      return TColorAttr( 0x1F );   // bright white command
            case rToolOut:   return TColorAttr( 0x17 );   // gray output
            case rHeading:   return TColorAttr( 0x1E );
            case rBold:      return TColorAttr( 0x1F );
            case rItalic:    return TColorAttr( 0x17 );
            case rBoldItalic:return TColorAttr( 0x17 );
            case rCodeInline:return TColorAttr( 0x71 );
            case rLink:      return TColorAttr( 0x19 );
            case rQuote:     return TColorAttr( 0x17 );
            case rMarker:    return TColorAttr( 0x1B );
            case rStrike:    return TColorAttr( 0x17 );
            case rDim:       return TColorAttr( 0x17 );
            case rCode:      return TColorAttr( 0x17 );
            case rKeyword:   return TColorAttr( 0x1E );
            case rType:      return TColorAttr( 0x1A );
            case rString:    return TColorAttr( 0x1D );
            case rChar:      return TColorAttr( 0x1D );
            case rComment:   return TColorAttr( 0x18 );
            case rNumber:    return TColorAttr( 0x1F );
            case rFunction:  return TColorAttr( 0x1B );
            case rPreproc:   return TColorAttr( 0x1D );
            case rOperator:  return TColorAttr( 0x17 );
            case rNormal:
            default:         return TColorAttr( 0x1E );
        }
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
    void emitThinking_( const Block &bl, int b, int width )
    {
        std::vector<Line> body = atui::tui::renderThinkingBody( bl.text, width );
        for( size_t i = 0; i < body.size(); ++i )
            lines_.push_back( RLine( body[i].spans, b, true, body[i].bg ) );
    }

    void closeOpen()
    {
        for( size_t i = 0; i < blocks_.size(); ++i )
            blocks_[i].open = false;
    }

    void relayout()
    {
        lines_.clear();
        int width = size.x > 0 ? size.x : 1;
        for( size_t b = 0; b < blocks_.size(); ++b )
        {
            if( !lines_.empty() )
                lines_.push_back( RLine() );   // blank spacer between blocks
            const Block &bl = blocks_[b];
            if( bl.kind == kUser )
            {
                std::vector<std::string> w =
                    wrapText_( "> " + bl.text, width );
                for( size_t i = 0; i < w.size(); ++i )
                    lines_.push_back( plainLine( w[i], rUser, (int) b, false ) );
            }
            else if( bl.kind == kStatus )
            {
                std::vector<std::string> w = wrapText_( bl.text, width );
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
                std::vector<std::string> rl = wrapText_( bl.out, outW );
                trimBlankRows_( rl );

                std::string cmd = "$ ";
                for( size_t i = 0; i < bl.text.size(); ++i )
                {
                    if( bl.text[i] == '\n' ) cmd += "\n  ";
                    else                     cmd += bl.text[i];
                }
                std::vector<std::string> cl = wrapText_( cmd, width );

                // The permission note, on its own row, marked like a comment so
                // it cannot be mistaken for the command or for its output.
                std::vector<std::string> nl;
                if( !bl.note.empty() )
                    nl = wrapText_( "  # " + bl.note, width );

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
                        : "  .. " + std::to_string(
                              (unsigned long) (rl.size() - shown) )
                          + " more lines (click to expand)";
                    lines_.push_back( plainLine( hint, rHead, (int) b, true ) );
                }
            }
            else if( bl.kind == kAnswer )
            {
                // Render as markdown: headings, emphasis, lists, tables and
                // syntax-highlighted code blocks become styled spans.
                std::vector<Line> md = atui::tui::render( bl.text, width );
                for( size_t i = 0; i < md.size(); ++i )
                    lines_.push_back(
                        RLine( md[i].spans, (int) b, false, md[i].bg ) );
            }
            else // kThinking
            {
                std::string head = bl.expanded ? "[-] thinking ("
                                               : "[+] thinking (";
                head += std::to_string( (unsigned long) bl.text.size() );
                head += bl.expanded ? " chars)  click/Ctrl-T to collapse"
                                    : " chars)  click/Ctrl-T to expand";
                std::vector<std::string> hw = wrapText_( head, width );
                for( size_t i = 0; i < hw.size(); ++i )
                    lines_.push_back( plainLine( hw[i], rHead, (int) b, true ) );
                if( bl.expanded )
                    emitThinking_( bl, (int) b, width );
            }
        }

        int contentH = (int) lines_.size();
        setLimit( 0, contentH );
        int maxTop = contentH - size.y;
        if( maxTop < 0 ) maxTop = 0;
        if( follow_ || delta.y > maxTop )
            scrollTo( 0, maxTop );
        drawView();
    }

    std::vector<Block> blocks_;
    std::vector<RLine> lines_;
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
            writeChar( 0, (short) r, ' ', rInput, (ushort) size.x );

        if( size.y >= 1 )
            writeChar( 0, 0, '-', rSep, (ushort) size.x );

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
                    writeStr( 0, (short)(r + 1), row.c_str(), rInput );
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

    static const uchar rInput = 1;
    static const uchar rSep   = 2;

    virtual TColorAttr mapColor( uchar index )
    {
        const bool dark = ( currentTheme() == ThemeDark );
        if( index == rSep )
            return TColorAttr( TColor( 0x3A4A63 ), TColor( 0x152238 ) );
        return TColorAttr( TColor( dark ? 0xF2E4AC : 0x000000 ),
                           TColor( dark ? 0x152238 : 0xFFFFFF ) );
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
#ifdef ATUI_HAVE_NETWORK
        g_oai_ = 0;
#endif
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
        delete g_oai_;
        g_oai_ = 0;
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
            pollSink_();
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

#ifdef ATUI_HAVE_NETWORK
    // Periodic poll of the AI token queue (worker thread -> event loop).
    if( TProgram::application != 0 )
        TProgram::application->setTimer( 50, 50 );
    // Install the tool-confirmation approver (edit_file / run_command).
    atui::tools::setApprover( &g_approver );
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

void pollSink_()
{
    if( g_transcript_ == 0 ) return;

#ifdef ATUI_HAVE_NETWORK
    if( !g_streamActive_ || g_oai_ == 0 ) return;

    bool gotToken = false;
    atui::net::Token tok;
    while( g_queue_.pop( tok ) )
    {
        switch( tok.kind )
        {
            case atui::net::tkThinking:   g_transcript_->appendThinking( tok.text );    break;
            case atui::net::tkStatus:     g_transcript_->appendStatus( tok.text );      break;
            case atui::net::tkTool:       g_transcript_->appendTool( tok.text );        break;
            case atui::net::tkToolNote:   g_transcript_->appendToolNote( tok.text );    break;
            case atui::net::tkToolResult: g_transcript_->appendToolResult( tok.text );  break;
            case atui::net::tkContent:    g_transcript_->appendAnswer( tok.text );      break;
        }
        gotToken = true;
    }

    // If the worker is blocked waiting for approval on a sensitive tool, show
    // the prompt now. g_approvalShowing prevents re-entrant dialogs while the
    // modal box pumps its own event loop (the 50ms timer keeps firing).
    if( !g_approvalShowing )
    {
        atui::tools::ApprovalRef req;
        {
            std::lock_guard< std::mutex > lock( g_apMx );
            req = g_pendingApproval;
        }
        if( req && !req->answered() )
        {
            g_approvalShowing = true;
            const int answer = showApprovalDialog_( req );
            req->answer( answer != apNo, answer == apSession );
            {
                std::lock_guard< std::mutex > lock( g_apMx );
                if( g_pendingApproval == req )
                    g_pendingApproval.reset();
            }
            g_approvalShowing = false;
        }
    }

    bool done = g_oai_->isDone();
    if( !gotToken && done && !g_replyFinished_ )
    {
        g_transcript_->finishReply();
        g_replyFinished_ = true;
        g_streamActive_ = false;
    }
#endif
}

void processMessage_()
{
    if( g_prompt_ == 0 || g_transcript_ == 0 ) return;

    std::string text = g_prompt_->text();
    String msg( text.c_str() );
    if( msg.size() <= 0 ) return;

#ifdef ATUI_HAVE_NETWORK
    if( !g_oai_ )
        g_oai_ = new atui::net::OaiChatModel();

    bool started = g_oai_->ask( std::string( msg.c_str(), msg.size() ),
                                std::string( g_history_.c_str(), g_history_.size() ),
                                &g_queue_ );
    if( !started )
    {
        // Busy: the previous reply is still streaming. Preserve the input text.
        g_transcript_->appendStatus( "(busy, try again)" );
        return;
    }

    g_streamActive_ = true;
    g_replyFinished_ = false;
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
    d->insert( new TStaticText( TRect( 3, 2, d->size.x - 2, d->size.y - 3 ),
        "atui - AI development TUI (M2)\nStreaming chat window.\n"
        "\n"
        "Switch model (menu bar) picks the provider and its model;\n"
        "Alt-P edits providers, Alt-T the theme, Ctrl-T the thinking blocks."
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
    memcpy( &rec[0], text.data(), n );
    line->setData( &rec[0] );
}

// Duplicate a std::string into a fresh new char[] (TStringCollection::freeItem
// releases its items with delete[], so plain strdup would be a mismatch).
static char *dupStr_( const std::string &s )
{
    char *p = new char[ s.size() + 1 ];
    memcpy( p, s.data(), s.size() );
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
    TMenuItem  *head_ = 0;
    TMenuItem **link_ = &head_;
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
        mb.add( new TMenuItem( label.c_str(),
                               (ushort) ( CmdSwitchModelBase + (int) i ),
                               kbNoKey ) );
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
    const std::string text = " " + menuText_( p.name, 16 ) + " | " +
                             menuText_( p.model, 24 ) + " ";
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

    int            editing = 0;         // provider the fields describe
    TInputLine    *name  = 0;
    TInputLine    *url   = 0;
    TInputLine    *key   = 0;
    TInputLine    *model = 0;
    TInputLine    *temp  = 0;
    TCheckBoxes   *resend = 0;
    TListBox      *providerList = 0;
    TListBox      *modelList = 0;
    TStaticText   *status = 0;
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
      TDialog( TRect( 0, 0, 72, 25 ), "Options - Providers" )
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
    snprintf( tbuf, sizeof tbuf, "%.2f", p.temperature );
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
            coll->insert( dupStr_( *it ) );
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
    status = new TStaticText( statusRect, text.c_str() );
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
    while( !u.empty() && ( u.back() == ' ' || u.back() == '\r' ||
                           u.back() == '\n' ) )
        u.pop_back();

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
    char msg[ 48 ];
    snprintf( msg, sizeof msg, "%zu model(s) loaded.", ids.size() );
    setStatus( msg );
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
    TInputLine    *wheel        = 0;
    TInputLine    *timeout      = 0;
    TRadioButtons *modes        = 0;
    TInputLine    *judgeModel   = 0;
    TInputLine    *judgeSeconds = 0;

    SettingsDialog();

    // The number in `line`, or `keep` when it holds none: clearing a field is
    // not a change, and 0 is outside every range here.
    static int numberIn( TInputLine *line, int keep );
};

SettingsDialog::SettingsDialog()
    : TWindowInit( &TDialog::initFrame ),
      TDialog( TRect( 0, 0, 66, 25 ), "Options - Settings" )
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
    snprintf( range, sizeof range, "%d to %d",
              atui::settings::kMinWheelLines, atui::settings::kMaxWheelLines );
    insert( new TStaticText( TRect( noteLeft, 2, size.x - 3, 3 ), range ) );

    insert( new TStaticText( TRect( 3, 4, 30, 5 ),
                             "Request timeout, minutes:" ) );
    timeout = new TInputLine( TRect( fieldLeft, 4,
                                     fieldLeft + kDigits + 2, 5 ), kDigits + 1 );
    insert( timeout );
    snprintf( range, sizeof range, "%d to %d",
              atui::net::kMinRequestMinutes, atui::net::kMaxRequestMinutes );
    insert( new TStaticText( TRect( noteLeft, 4, size.x - 3, 5 ), range ) );

    // Permissions: an enum is meant for radio buttons - the three modes are
    // choices, not numbers, and the parenthesised name is what goes in the
    // config file and in permissions.log, so the wording here can be revised
    // without the user losing the thread.
    insert( new TStaticText( TRect( 3, 6, size.x - 3, 7 ),
                             "Before running something:" ) );
    modes = new TRadioButtons(
        TRect( 5, 7, size.x - 4, 10 ),
        new TSItem( "Always ask before a tool runs (ask_all)",
        new TSItem( "In-project work is fine, ask outside (exact)",
        new TSItem( "A model judges what the facts cannot settle (agent)",
                    NULL ) ) ) );
    insert( modes );
    ushort chosen = ( atui::tools::permissionMode() ==
                      atui::tools::pmExact ) ? 1
                    : ( atui::tools::permissionMode() ==
                        atui::tools::pmAgent ) ? 2 : 0;
    modes->setData( &chosen );

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
    snprintf( range, sizeof range, "%d to %d",
              atui::tools::kMinJudgeSeconds, atui::tools::kMaxJudgeSeconds );
    insert( new TStaticText( TRect( fieldLeft + 6, 12, size.x - 3, 13 ),
                             range ) );

    setInput_( wheel, std::to_string( atui::settings::wheelLines() ) );
    setInput_( timeout, std::to_string( atui::net::requestMinutes() ) );
    setInput_( judgeModel, atui::tools::judgeModelOverride() );
    setInput_( judgeSeconds, std::to_string( atui::tools::judgeSeconds() ) );

    // TStaticText clips rather than wraps, so this is laid out in lines that
    // fit the dialog.
    // TStaticText clips rather than wraps, and a clipped line is worse than no
    // line: it looks like the dialog has nothing more to say. Every row here is
    // laid out to fit, and the box is as tall as the text needs.
    insert( new TStaticText( TRect( 3, 14, size.x - 3, 23 ),
        "One wheel notch scrolls the chat by that many lines; the\n"
        "timeout is how long to wait for the server on a prompt -\n"
        "per request, so a tool round gets it again. A field with\n"
        "no number in it keeps the value it has.\n"
        "\n"
        "Judging is done by a second model: name a cheap one, or\n"
        "leave it blank to use the model you are chatting with.\n"
        "Nothing outside the project directory is done unasked." ) );

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

        std::string model( d->judgeModel->data );
        const size_t from = model.find_first_not_of( " \t" );
        const size_t to   = model.find_last_not_of( " \t" );
        atui::tools::judgeModelOverride() =
            ( from == std::string::npos ) ? std::string()
                                          : model.substr( from, to - from + 1 );

        atui::tools::judgeSeconds() = atui::tools::clampJudgeSeconds(
            SettingsDialog::numberIn( d->judgeSeconds,
                                      atui::tools::judgeSeconds() ) );
        updatePermissionStatus_();

        atui::settings::saveAll();
    }
    destroy( d );
}

int run_app()
{
    // Apply persisted settings (theme + provider) before the UI is built,
    // so the palette and endpoint reflect the saved config.
    atui::settings::loadAll();

    TApp app;
    app.run();
    return 0;
}

} // namespace atui::tui
} // namespace atui
