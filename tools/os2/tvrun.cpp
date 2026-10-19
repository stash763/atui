// tvision on OS/2, staying up: what does it look like, and do the keys arrive?
//
// tvhello proved the whole path runs -- screen up, dialog drawn, one event read,
// screen restored, exit 0 -- but it exits on the first keystroke, which is no way
// to look at a screen.  This is the same start-up with an event loop of its own
// around a dialog, so the frame can be captured while it is up and keys sent to it
// one at a time.
//
// The loop is TGroup::execute()'s own shape -- getEvent, handleEvent, repeat --
// rather than TProgram::run(), so that quitting does not depend on how cmQuit
// travels through the status line and the modal chain: Esc, Alt-X and Ctrl-C each
// end it here.  Everything else is the library's own plumbing.
//
//   build.cmd link tvrun.cpp tvrun.exe
//   tvrun.exe
//     then, from the workstation:
//       tui-test text --session os2tv            the screen as the driver drew it
//       tui-test cells --session os2tv           the attribute bytes
//       tui-test key press --session os2tv tab   and watch the focus move
//
// d:\atui\tvrun.log says how far it got if it does not come up.

#define Uses_TApplication
#define Uses_TButton
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TLabel
#define Uses_TPalette
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScreen
#define Uses_TStaticText
#define Uses_TView
#include <tvision/tv.h>

#include <stdio.h>

static FILE *logFile = NULL;

static void step( const char *what )
{
    if( logFile != NULL )
        {
        fprintf( logFile, "ok %s\n", what );
        fflush( logFile );
        }
}

// Which view holds the focus, identified by its size: the input line is 32x1, the
// buttons 12x2 and 14x2, the dialog 44x11.
static void tell( const char *tag, TGroup *where )
{
    TView *f = where->current;
    fprintf( logFile, "  %s: focus=%p", tag, (void *) f );
    if( f != NULL )
        fprintf( logFile, " size=(%d,%d) origin=(%d,%d)",
                 (int) f->size.x, (int) f->size.y, (int) f->origin.x, (int) f->origin.y );
    fprintf( logFile, "\n" );
    fflush( logFile );
}

class TvRun : public TApplication
{

public:

    TvRun() :
        TProgInit( &TProgram::initStatusLine,
                   &TProgram::initMenuBar,
                   &TProgram::initDeskTop ),
        TApplication()
    {
    }

};

int main()
{
    logFile = fopen( "d:\\atui\\tvrun.log", "w" );
    step( "main" );

    TvRun app;
    step( "application" );

    TDialog *dialog = new TDialog( TRect( 18, 6, 62, 17 ), "atui on OS/2" );
    dialog->insert( new TLabel( TRect( 3, 3, 41, 4 ), "classic tvision layer on OS/2", 0 ) );
    dialog->insert( new TLabel( TRect( 3, 5, 9, 6 ), "Name", 0 ) );
    TInputLine *nameField = new TInputLine( TRect( 9, 5, 41, 6 ), 40 );
    dialog->insert( nameField );
    dialog->insert( new TButton( TRect( 14, 9, 26, 11 ), "~O~K", cmOK, bfDefault ) );
    dialog->insert( new TButton( TRect( 28, 9, 42, 11 ), "~C~ancel", cmCancel, bfNormal ) );
    TProgram::deskTop->insert( dialog );
    dialog->select();                 // focus the dialog in the desktop
    nameField->select();              // and the first field inside it
    step( "dialog inserted" );
    tell( "after insert", dialog );

    TEvent event;
    int quit = 0;
    while( !quit )
        {
        app.getEvent( event );
        if( event.what == evKeyDown &&
            (event.keyDown.keyCode == kbEsc ||
             event.keyDown.keyCode == kbAltX ||
             event.keyDown.keyCode == kbCtrlC)
          )
            {
            fprintf( logFile, "  key %04x ends the loop\n", (unsigned) event.keyDown.keyCode );
            quit = 1;
            }
        else
            {
            switch( event.what )
                {
            case evKeyDown:
                fprintf( logFile, "  key %04x text '%c' len %d\n",
                         (unsigned) event.keyDown.keyCode,
                         event.keyDown.textLength > 0 ? event.keyDown.text[0] : ' ',
                         (int) event.keyDown.textLength );
                break;
            case evMouseDown:
            case evMouseUp:
                fprintf( logFile, "  mouse %s at (%d,%d) buttons=%02x flags=%02x\n",
                         event.what == evMouseDown ? "down" : "up",
                         (int) event.mouse.where.x, (int) event.mouse.where.y,
                         (unsigned) event.mouse.buttons, (unsigned) event.mouse.eventFlags );
                break;
            case evMouseMove:
                fprintf( logFile, "  mouse move at (%d,%d)\n",
                         (int) event.mouse.where.x, (int) event.mouse.where.y );
                break;
            case evCommand:
                fprintf( logFile, "  command %04x\n", (unsigned) event.message.command );
                break;
            case evBroadcast:
                fprintf( logFile, "  broadcast %04x\n", (unsigned) event.message.command );
                break;
            default:
                fprintf( logFile, "  event what=%d\n", (int) event.what );
                break;
                }
            fflush( logFile );
            app.handleEvent( event );
            tell( "after event", dialog );
            }
        }

    step( "leaving" );
    return 0;
}
