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
#define Uses_MsgBox
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <tvision/tv.h>

#include "app.h"
#include "core/ai.h"
#include "tui/theme.h"
#include "tui/md_style.h"
#include "tui/markdown.h"
#include "net/provider.h"
#include "settings/settings.h"

#ifdef ATUI_HAVE_NETWORK
#include "net/oai_chat_model.h"
#include "net/oai_client.h"
#include "net/token_queue.h"
#include "tools/approval.h"
#include <mutex>
#endif

#include <vector>

namespace atui {
namespace tui {

using atui::core::String;

enum { CmdHelp = 1000, CmdTheme = 1004, CmdProviders = 1003,
       CmdRefreshModels = 2000 };

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

// Show the confirmation dialog (modal; runs a nested event loop on this
// thread) and return true if the user allowed it.
bool showApprovalDialog_( const atui::tools::ApprovalRef &req )
{
    std::string msg = "The model wants to run the \"" + req->tool
                    + "\" tool.\n\n";
    msg += req->summary;
    msg += "\n\nAllow this to run?";
    ushort r = ::messageBox( msg.c_str(),
                             mfConfirmation | mfYesButton | mfNoButton );
    return r == cmYes;
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
// collapsible blocks, and toggle a block with a mouse click or Ctrl-T. Derives
// from TScroller for free scrollbar + scrolling. Rendering is span-based: each
// row is a vector of Spans whose Role mapColor() turns into a color.
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
            int d = ( event.mouse.wheel & mwDown ) ? 1
                  : ( ( event.mouse.wheel & mwUp ) ? -1 : 0 );
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
                    blocks_[b].kind == kThinking )
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
    enum Kind { kUser, kAnswer, kThinking, kStatus };
    struct Block { int kind; std::string text; bool expanded; bool open; };
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

    void closeOpen()
    {
        for( size_t i = 0; i < blocks_.size(); ++i )
            blocks_[i].open = false;
    }

    // Wrap one logical string (split on '\n') into display rows of `width`.
    static void wrapOne( const std::string &seg, int width,
                         std::vector<std::string> &out )
    {
        if( width < 1 ) width = 1;
        if( seg.empty() ) { out.push_back( "" ); return; }
        size_t i = 0, n = seg.size();
        while( i < n )
        {
            size_t k = i, cnt = 0, lastSpace = std::string::npos;
            while( k < n && cnt < (size_t) width )
            {
                if( seg[k] == ' ' ) lastSpace = k;
                ++k; ++cnt;
            }
            if( k < n && lastSpace != std::string::npos && lastSpace > i )
            {
                out.push_back( seg.substr( i, lastSpace - i ) );
                i = lastSpace + 1;
                while( i < n && seg[i] == ' ' ) ++i;
            }
            else
            {
                out.push_back( seg.substr( i, k - i ) );
                i = k;
            }
        }
    }

    static std::vector<std::string> wrapText( const std::string &s, int width )
    {
        std::vector<std::string> out;
        size_t start = 0;
        while( true )
        {
            size_t nl = s.find( '\n', start );
            std::string seg = ( nl == std::string::npos )
                ? s.substr( start ) : s.substr( start, nl - start );
            wrapOne( seg, width, out );
            if( nl == std::string::npos ) break;
            start = nl + 1;
        }
        return out;
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
                    wrapText( "> " + bl.text, width );
                for( size_t i = 0; i < w.size(); ++i )
                    lines_.push_back( plainLine( w[i], rUser, (int) b, false ) );
            }
            else if( bl.kind == kStatus )
            {
                std::vector<std::string> w = wrapText( bl.text, width );
                for( size_t i = 0; i < w.size(); ++i )
                    lines_.push_back( plainLine( w[i], rStatus, (int) b, false ) );
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
                std::vector<std::string> hw = wrapText( head, width );
                for( size_t i = 0; i < hw.size(); ++i )
                    lines_.push_back( plainLine( hw[i], rHead, (int) b, true ) );
                if( bl.expanded )
                {
                    std::vector<std::string> bw = wrapText( bl.text, width - 3 );
                    for( size_t i = 0; i < bw.size(); ++i )
                        lines_.push_back(
                            plainLine( "   " + bw[i], rThinking, (int) b, true ) );
                }
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
            hideCursor();
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

    // Byte offset where wrapped row i begins.
    size_t rowStart( int i )
    {
        size_t s = 0;
        for( int k = 0; k < i; ++k ) s += rows_[k].size();
        return s;
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
        if( w < 1 ) w = 1;
        if( buf_.empty() ) { rows_.push_back( std::string() ); return; }

        size_t i = 0, n = buf_.size();
        while( i < n )
        {
            if( (int)(n - i) <= w )
            {
                rows_.push_back( buf_.substr( i ) );
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
    std::vector<std::string> rows_;
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
            switch( event.message.command )
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
    return new TStatusLine( r,
        *new TStatusDef( 0, 0xFFFF )
        + *new TStatusItem( "~Alt-H~  About", kbAltH, CmdHelp )
        + *new TStatusItem( " Ctrl-T  thinking", 0, 0 )
        + *new TStatusItem( "~Alt-X~  Quit",  kbAltX, cmQuit )
    );
}

TMenuBar *initMenuBar_( TRect r )
{
    r.b.y = r.a.y + 1;
    return new TMenuBar( r,
        *new TSubMenu( "~F~ile", kbAltF )
        + *new TMenuItem( "H~elp...",  CmdHelp, kbAltH, hcNoContext, "Alt-H" )
        + newLine()
        + *new TMenuItem( "~Options", kbAltO,
              new TMenu(
                  *new TMenuItem( "~P~roviders...", CmdProviders, kbAltP,
                                  hcNoContext, "Alt-P" )
                  + newLine()
                  + *new TMenuItem( "~T~heme...", CmdTheme, kbAltT,
                                    hcNoContext, "Alt-T" ) ) )
        + newLine()
        + *new TMenuItem( "E~xit...",  cmQuit,  kbAltX, hcNoContext, "Alt-X" )
        + newLine()
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
    // panels share the desktop exactly (no gap, no overlap). The scrollbar is 3
    // cells wide (a light frame + the bar + frame), so it occupies cols x2-3..x2-1.
    TRect sbRect( x2 - 3, y1 + 1, x2, y2 - 1 );
    TRect trRect( x1, y1 + 1, x2 - 3, y2 - 1 );
    TRect inRect( x1, y2 - 2, x2 - 1, y2 - 1 );

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
#endif

    appendReply_( "atui (M2): type a message and press Enter." );
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

    const TRect pRect( x1, ptop, x2 - 1, bottom );
    const TRect tRect( x1, y1 + 1, x2 - 3, ptop );
    const TRect bRect( x2 - 3, y1 + 1, x2, ptop );   // 3 cells: cols x2-3..x2-1

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
            case atui::net::tkThinking: g_transcript_->appendThinking( tok.text ); break;
            case atui::net::tkStatus:   g_transcript_->appendStatus( tok.text );   break;
            case atui::net::tkContent:  g_transcript_->appendAnswer( tok.text );   break;
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
            bool allow = showApprovalDialog_( req );
            req->answer( allow );
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
        "atui - AI development TUI (M2)\nStreaming chat window."
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

// Options > Providers dialog. Besides the endpoint fields it shows an
// "Available models" list box populated from the server's GET /models endpoint
// (a tvision TListBox + TScrollBar, the tvdemo fileview/listdlg pattern).
// Clicking a model copies it into the Model field; "Refresh" reloads the list
// from the URL/Key fields as currently typed. Model fetching needs the network
// build; without it the list stays empty and the status line says why.
class ProvidersDialog : public TDialog
{
public:

    TInputLine  *url    = 0;
    TInputLine  *key    = 0;
    TInputLine  *model  = 0;
    TInputLine  *temp   = 0;
    TCheckBoxes *resend = 0;
    TListBox    *modelList = 0;
    TStaticText *status = 0;
    TRect        statusRect;

    ProvidersDialog();

    void refreshModels();
    void setStatus( const std::string &text );

    virtual void handleEvent( TEvent& event )
    {
        if( event.what == evCommand &&
            event.message.command == CmdRefreshModels )
        {
            refreshModels();
            clearEvent( event );
        }
        else if( event.what == evBroadcast &&
                 event.message.command == cmListItemSelected &&
                 event.message.infoPtr == (void *) modelList )
        {
            TCollection *items = modelList->list();
            short sel = modelList->focused;
            if( items && sel >= 0 && sel < items->getCount() )
            {
                char buf[ 160 ];
                modelList->getText( buf, sel, sizeof buf - 1 );
                buf[ sizeof buf - 1 ] = '\0';
                setInput_( model, buf );
            }
            clearEvent( event );
        }
        else
            TDialog::handleEvent( event );
    }
};

ProvidersDialog::ProvidersDialog()
    : TWindowInit( &TDialog::initFrame ),
      TDialog( TRect( 10, 3, 76, 28 ), "Options - Providers" )
{
    insert( new TStaticText( TRect( 3, 1, size.x - 2, 2 ),
        "Remote llama.cpp server (OpenAI-compatible):" ) );

    url   = new TInputLine( TRect( 14, 3, size.x - 3, 4 ), 128 );
    key   = new TInputLine( TRect( 14, 5, size.x - 3, 6 ), 64 );
    model = new TInputLine( TRect( 14, 7, size.x - 3, 8 ), 128 );
    temp  = new TInputLine( TRect( 14, 9, 22, 10 ), 8 );

    insert( new TLabel( TRect( 3, 3, 13, 4 ), "~U~rl", url ) );
    insert( url );
    insert( new TLabel( TRect( 3, 5, 13, 6 ), "~K~ey", key ) );
    insert( key );
    insert( new TLabel( TRect( 3, 7, 13, 8 ), "~M~odel", model ) );
    insert( model );
    insert( new TLabel( TRect( 3, 9, 13, 10 ), "~T~emp", temp ) );
    insert( temp );

    resend = new TCheckBoxes( TRect( 3, 11, size.x - 3, 12 ),
        new TSItem( "~R~esend thinking in history", NULL ) );
    insert( resend );

    // Available models list box (with its own vertical scrollbar) + Refresh.
    insert( new TLabel( TRect( 3, 13, 20, 14 ), "~A~vailable models", 0 ) );
    TButton *refresh = new TButton( TRect( size.x - 16, 13, size.x - 3, 15 ),
                                    "~R~efresh", CmdRefreshModels, bfNormal );
    insert( refresh );

    TScrollBar *sb = new TScrollBar(
        TRect( size.x - 3, 14, size.x - 2, 20 ) );
    insert( sb );
    modelList = new ModelListBox( TRect( 3, 14, size.x - 4, 20 ), 1, sb );
    modelList->newList( new TStringCollection( 4096, 32 ) );
    insert( modelList );

    statusRect = TRect( 3, 21, size.x - 3, 22 );
    setStatus( "" );

    TButton *ok     = new TButton( TRect( 0, 0, 14, 3 ), "OK",     cmOK,     bfDefault );
    TButton *cancel = new TButton( TRect( 0, 0, 14, 3 ), "Cancel", cmCancel, bfNormal );
    insert( ok );
    insert( cancel );
    ok->moveTo( (size.x - 30) / 2, size.y - 3 );
    cancel->moveTo( ok->origin.x + 16, size.y - 3 );
}

// TStaticText has no text setter, so the status line is replaced in place.
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

void ProvidersDialog::refreshModels()
{
    std::string u( url->data );
    std::string k( key->data );
    // Trim trailing whitespace/newline that can slip into the input buffer.
    while( !u.empty() && ( u.back() == ' ' || u.back() == '\r' ||
                           u.back() == '\n' ) )
        u.pop_back();

#ifdef ATUI_HAVE_NETWORK
    std::vector<std::string> ids;
    std::string err;
    if( !atui::net::fetchModels( u, k, ids, err ) )
    {
        modelList->newList( new TStringCollection( 4096, 32 ) );
        std::string m = "Refresh failed: " + err;
        setStatus( m );
        return;
    }
    TStringCollection *coll = new TStringCollection( 4096, 32 );
    std::string want( model->data );
    int selIdx = -1, i = 0;
    for( std::vector<std::string>::const_iterator it = ids.begin();
         it != ids.end(); ++it, ++i )
    {
        if( selIdx < 0 && *it == want ) selIdx = i;
        coll->insert( dupStr_( *it ) );
    }
    modelList->newList( coll );
    if( selIdx >= 0 ) modelList->focusItem( (short) selIdx );
    char msg[ 48 ];
    snprintf( msg, sizeof msg, "%zu model(s) loaded.", ids.size() );
    setStatus( msg );
#else
    (void) u; (void) k;
    modelList->newList( new TStringCollection( 4096, 32 ) );
    setStatus( "Model list needs a network build." );
#endif
}

// Options > Providers: configure the remote llama.cpp server endpoint.
// Edits are written to the shared providerSettings() on OK, so the next
// chat request talks to the new endpoint.
void TApp::showProviders()
{
    atui::net::ProviderSettings &p = atui::net::providerSettings();

    ProvidersDialog *d = new ProvidersDialog();

    setInput_( d->url, p.baseUrl );
    setInput_( d->key, p.apiKey );
    setInput_( d->model, p.model );
    char tbuf[16];
    snprintf( tbuf, sizeof tbuf, "%.2f", p.temperature );
    setInput_( d->temp, tbuf );
    ushort rinit = p.thinkingResend ? 1 : 0;
    d->resend->setData( &rinit );

    d->refreshModels();   // best-effort populate on open

    if( deskTop->execView( d ) == cmOK )
    {
        std::string u( d->url->data );
        std::string k( d->key->data );
        std::string m( d->model->data );
        std::string tv( d->temp->data );
        if( !u.empty() ) p.baseUrl = u;
        p.apiKey = k;                       // clearing the key is allowed
        if( !m.empty() ) p.model = m;
        if( !tv.empty() )
        {
            double x = std::atof( tv.c_str() );
            if( x >= 0.0 ) p.temperature = x;
        }
        ushort rval = 0;
        d->resend->getData( &rval );
        p.thinkingResend = ( rval & 0x0001 ) != 0;
        atui::settings::saveAll();
    }
    destroy( d );
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
