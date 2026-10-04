// Bare atui TUI shell (M0/M1): menu bar, status line, About dialog, exit,
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
#define Uses_TStatusLine
#define Uses_TStatusDef
#define Uses_TStatusItem
#define Uses_TDialog
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TDeskTop
#define Uses_TBackground
#define Uses_TTerminal
#define Uses_TTextDevice
#define Uses_TInputLine
#define Uses_TScrollBar

#include <cstdio>

#include <tvision/tv.h>

#include "app.h"
#include "core/ai.h"

// Debug event log (only when built with ATUI_DEBUG_KEYS).
#ifdef ATUI_DEBUG_KEYS
#include <fstream>
static std::ofstream g_keylog;
#endif

namespace atui {
namespace tui {

using atui::core::String;
using atui::core::IChatModel;

enum { CmdHelp = 1000 };

namespace {
    TTerminal *g_transcript_ = 0;
    TInputLine *g_inputLine_ = 0;
    TScrollBar *g_vBar_ = 0;
    IChatModel *g_model_ = 0;
    String g_history_;
}

// Module helpers (outer namespace; definitions appear after TApp).
TStatusLine *initStatusLine_( TRect r );
TMenuBar *initMenuBar_( TRect r );
TDeskTop *initDeskTop_( TRect r );
void appendUser_( const char *text );
void appendReply_( const char *text );
void processMessage_();
void focusInput_();

class TApp : public TApplication
{
public:
    TApp()
        : TProgInit( &TApp::initStatusLine,
                      &TApp::initMenuBar,
                      &TApp::initDeskTop )
    {
        g_model_ = new atui::core::ChatModelStub();
#ifdef ATUI_DEBUG_KEYS
        g_keylog = std::ofstream( "/tmp/atui_keylog.txt" );
        if( !g_keylog ) g_keylog << "keylog open failed\n";
#endif
    }

    ~TApp()
    {
        delete g_model_;
        g_model_ = 0;
    }

    virtual void handleEvent( TEvent& event )
    {
        TApplication::handleEvent( event );

#ifdef ATUI_DEBUG_KEYS
        g_keylog << "what=" << (int)event.what
                 << " keyCode=" << (int)event.keyDown.keyCode
                 << " mods=" << (int)event.keyDown.controlKeyState
                 << " text=" << (event.keyDown.textLength ? std::string(event.keyDown.text, (size_t)event.keyDown.textLength) : std::string(""))
                 << "\n";
        g_keylog.flush();
#endif

        if( event.what == evCommand )
        {
            switch( event.message.command )
            {
                case CmdHelp:
                    showHelp();
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
        // before the input line can consume the key.
        if( g_inputLine_ != 0 &&
            event.what == evKeyDown &&
            event.keyDown.keyCode == kbEnter &&
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
        + *new TMenuItem( "E~xit...",  cmQuit,  kbAltX, hcNoContext, "Alt-X" )
        + newLine()
        + *new TSubMenu( "~A~bout", kbAltA )
        + *new TMenuItem( "ab~out...", CmdHelp, kbAltA, hcNoContext, "Alt-A" )
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
    g_transcript_ = new TTerminal( trRect, 0, g_vBar_, 65535 );
    g_transcript_->hideCursor();
    d->insert( g_transcript_ );

    g_inputLine_ = new TInputLine( inRect, 256, 0, ilMaxBytes );
    d->insert( g_inputLine_ );

    appendReply_( "atui (M1): type a message and press Enter." );
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
    g_transcript_->do_sputn( line.c_str(), line.size() );
}

void appendReply_( const char *text )
{
    String line;
    line += text;
    line += "\n\n";
    g_history_ += line.c_str();
    g_transcript_->do_sputn( line.c_str(), line.size() );
}

void processMessage_()
{
    if( g_inputLine_ == 0 || g_transcript_ == 0 || g_model_ == 0 ) return;

    String msg( g_inputLine_->data );
    if( msg.size() <= 0 ) return;

    appendUser_( msg.c_str() );
    String reply = g_model_->ask( msg, g_history_ );
    appendReply_( reply.c_str() );

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
        "atui - AI development TUI (M1)\nChat window wired to a stub model."
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

int run_app()
{
    TApp app;
    app.run();
    return 0;
}

} // namespace atui::tui
} // namespace atui
