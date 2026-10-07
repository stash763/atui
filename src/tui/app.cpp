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
#endif

#include <vector>

namespace atui {
namespace tui {

using atui::core::String;

enum { CmdHelp = 1000, CmdTheme = 1004, CmdProviders = 1003,
       CmdRefreshModels = 2000 };

class TranscriptView;   // defined below; used via pointer in the globals

namespace {
    TranscriptView *g_transcript_ = 0;
    TInputLine *g_inputLine_ = 0;
    TScrollBar *g_vBar_ = 0;
    atui::core::ChatModelStub *g_stub_ = 0;
    String g_history_;

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
void pollSink_();

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
            writeChar( 0, row, ' ', rNormal, (ushort) size.x );
            int li = top + row;
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
        RLine() : spans(), block( -1 ), toggle( false ) {}
        RLine( const std::vector<Span> &s, int b, bool g )
            : spans( s ), block( b ), toggle( g ) {}
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
                    lines_.push_back( RLine( md[i].spans, (int) b, false ) );
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

        // Periodic poll of the AI token queue (timer -> evBroadcast/cmTimerExpired).
        if( event.what == evBroadcast && event.message.command == cmTimerExpired )
        {
            pollSink_();
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

        // Intercept Enter in the chat input line and route it to the chat flow,
        // before the input line can consume the key. Some terminals report
        // Enter as a key-down, others as a command event; handle both.
        bool enterPressed =
            ( event.what == evKeyDown && event.keyDown.keyCode == kbEnter ) ||
            ( event.what == evCommand  && event.message.command == kbEnter );
        if( g_inputLine_ != 0 && enterPressed &&
            (g_inputLine_->state & sfSelected) != 0 )
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

    int inHeight = 3;

    TRect inRect( x1, y2 - inHeight, x2 - 1, y2 - 1 );
    TRect sbRect( x2 - 2, y1 + 1, x2 - 1, y2 - inHeight - 1 );

    g_vBar_ = new TScrollBar( sbRect );
    d->insert( g_vBar_ );

    TRect trRect( x1, y1 + 1, x2 - 3, y2 - inHeight - 1 );
    g_transcript_ = new TranscriptView( trRect, g_vBar_ );
    g_transcript_->hideCursor();
    g_transcript_->setThinkingExpandAll( atui::settings::thinkingExpandAll() );
    d->insert( g_transcript_ );

    g_inputLine_ = new TInputLine( inRect, 256, 0, ilMaxBytes );
    d->insert( g_inputLine_ );

#ifdef ATUI_HAVE_NETWORK
    // Periodic poll of the AI token queue (worker thread -> event loop).
    if( TProgram::application != 0 )
        TProgram::application->setTimer( 50, 50 );
#endif

    appendReply_( "atui (M2): type a message and press Enter." );
    focusInput_();
    return d;
}

void focusInput_()
{
    if( g_inputLine_ == 0 ) return;
    g_inputLine_->data[0] = EOS;
    g_inputLine_->curPos = 0;
    g_inputLine_->firstPos = 0;
    g_inputLine_->selStart = g_inputLine_->selEnd = 0;
    g_inputLine_->drawView();
    g_inputLine_->focus();
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
    if( g_inputLine_ == 0 || g_transcript_ == 0 ) return;

    String msg( g_inputLine_->data );
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

    // Clear the input line for the next message.
    g_inputLine_->data[0] = EOS;
    g_inputLine_->curPos = 0;
    g_inputLine_->firstPos = 0;
    g_inputLine_->selStart = g_inputLine_->selEnd = 0;
    g_inputLine_->drawView();
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
