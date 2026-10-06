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
#define Uses_TSItem
#define Uses_TDeskTop
#define Uses_TBackground
#define Uses_TTerminal
#define Uses_TTextDevice
#define Uses_TInputLine
#define Uses_TScrollBar
#define Uses_TTimer
#define Uses_TScreen
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include <tvision/tv.h>

#include "app.h"
#include "core/ai.h"
#include "tui/theme.h"
#include "net/provider.h"
#include "settings/settings.h"

#ifdef ATUI_HAVE_NETWORK
#include "net/oai_chat_model.h"
#include "net/token_queue.h"
#endif

namespace atui {
namespace tui {

using atui::core::String;

enum { CmdHelp = 1000, CmdTheme = 1004, CmdProviders = 1003 };

namespace {
    TTerminal *g_transcript_ = 0;
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

// The chat transcript uses the classic Turbo Vision text-display colors
// (yellow on blue, app palette entry 13 — as in tvdemo's file viewer)
// instead of the stock TScroller mapping, which resolves to entry 6
// (dark gray on green).
class ChatTerminal : public TTerminal
{
public:
    ChatTerminal( const TRect& bounds, TScrollBar *aVBar,
                  ushort aBufSize ) noexcept :
        TTerminal( bounds, 0, aVBar, aBufSize ) {}

    virtual TPalette& getPalette() const
    {
        // Index 1 -> app palette entry 13 (yellow on blue);
        // index 2 -> entry 14 (unused by TTerminal::draw).
        static const char cpChatTerminal[] = "\x0D\x0E";
        static TPalette palette( cpChatTerminal, sizeof( cpChatTerminal )-1 );
        return palette;
    }
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
    g_transcript_ = new ChatTerminal( trRect, g_vBar_, 65535 );
    g_transcript_->hideCursor();
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

void pollSink_()
{
    if( g_transcript_ == 0 ) return;

#ifdef ATUI_HAVE_NETWORK
    if( !g_streamActive_ || g_oai_ == 0 ) return;

    bool gotToken = false;
    std::string tok;
    while( g_queue_.pop( tok ) )
    {
        g_transcript_->do_sputn( tok.c_str(), tok.size() );
        gotToken = true;
    }
    bool done = g_oai_->isDone();
    if( !gotToken && done && !g_replyFinished_ )
    {
        g_transcript_->do_sputn( "\n\n", 2 );
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
        g_transcript_->do_sputn( "(busy, try again)\n\n", 17 );
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

// Options > Providers: configure the remote llama.cpp server endpoint.
// Edits are written to the shared providerSettings() on OK, so the next
// chat request talks to the new endpoint.
void TApp::showProviders()
{
    atui::net::ProviderSettings &p = atui::net::providerSettings();

    TDialog *d = new TDialog( TRect( 12, 7, 72, 21 ),
                              "Options - Providers" );
    d->insert( new TStaticText( TRect( 3, 1, d->size.x - 2, 2 ),
        "Remote llama.cpp server (OpenAI-compatible):" ) );

    TInputLine *url   = new TInputLine( TRect( 14, 3, d->size.x - 3, 4 ), 128 );
    TInputLine *key   = new TInputLine( TRect( 14, 5, d->size.x - 3, 6 ), 64 );
    TInputLine *model = new TInputLine( TRect( 14, 7, d->size.x - 3, 8 ), 64 );
    TInputLine *temp  = new TInputLine( TRect( 14, 9, 22, 10 ), 8 );

    d->insert( new TLabel( TRect( 3, 3, 13, 4 ), "~U~rl", url ) );
    d->insert( url );
    d->insert( new TLabel( TRect( 3, 5, 13, 6 ), "~K~ey", key ) );
    d->insert( key );
    d->insert( new TLabel( TRect( 3, 7, 13, 8 ), "~M~odel", model ) );
    d->insert( model );
    d->insert( new TLabel( TRect( 3, 9, 13, 10 ), "~T~emp", temp ) );
    d->insert( temp );

    setInput_( url, p.baseUrl );
    setInput_( key, p.apiKey );
    setInput_( model, p.model );
    char tbuf[16];
    snprintf( tbuf, sizeof tbuf, "%.2f", p.temperature );
    setInput_( temp, tbuf );

    TButton *ok     = new TButton( TRect( 0, 0, 14, 3 ), "OK",     cmOK,     bfDefault );
    TButton *cancel = new TButton( TRect( 0, 0, 14, 3 ), "Cancel", cmCancel, bfNormal );
    d->insert( ok );
    d->insert( cancel );
    ok->moveTo( (d->size.x - 30) / 2, d->size.y - 3 );
    cancel->moveTo( ok->origin.x + 16, d->size.y - 3 );

    if( deskTop->execView( d ) == cmOK )
    {
        std::string u( url->data );
        std::string k( key->data );
        std::string m( model->data );
        std::string tv( temp->data );
        if( !u.empty() ) p.baseUrl = u;
        p.apiKey = k;                       // clearing the key is allowed
        if( !m.empty() ) p.model = m;
        if( !tv.empty() )
        {
            double x = std::atof( tv.c_str() );
            if( x >= 0.0 ) p.temperature = x;
        }
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
