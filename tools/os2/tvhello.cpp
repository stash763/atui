// The first tvision program on OS/2: what does the classic layer still need?
//
// hello.cpp proved the toolchain links and runs.  This asks the port's question:
// the classic classes are all compiled, but a program that uses them needs a
// console, and the driver that provides one -- the bodies of THardwareInfo -- is
// Borland-only in this library (hardwrvr.cpp guards them with
// `#ifdef __BORLANDC__`), so Watcom has the class and not the implementation.
// Linking this file is what says exactly which functions that leaves missing:
// the compiler has an opinion about what a console driver should contain, and
// only the linker has to be convinced.
//
// What it does is deliberately the whole path and nothing extra: constructing a
// TApplication brings the screen up through the driver (TAppInit constructs
// THardwareInfo, TScreen, TEventQueue); inserting a dialog marks views exposed;
// drawing the desktop hands the driver cells through screenWrite; getEvent then
// waits for one key, which is the input side.  No invented test scaffolding --
// an ordinary dialog, the palette atui would use.
//
//   build.cmd link tvhello.cpp tvhello.exe
//   tvhello.exe            press any key: it restores the screen and exits
//
// If this links before the driver exists, it has not asked hard enough.

#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TKeys
#define Uses_TPalette
#define Uses_TProgram
#define Uses_TRect
#define Uses_TView
#define Uses_TWindow
#include <tvision/tv.h>

class TvHello : public TApplication
{

public:

    // TProgInit is a virtual base, so the most derived class has to name it:
    // the three create-functions are TProgram's own statics, which is what
    // TApplication passes too.  Forgetting it is E390 "cannot generate default
    // constructor to initialize 'TProgInit'" rather than anything clearer.
    TvHello() :
        TProgInit( &TProgram::initStatusLine,
                   &TProgram::initMenuBar,
                   &TProgram::initDeskTop ),
        TApplication()
    {
    }

};

int main()
{
    TvHello app;

    TDialog *dialog = new TDialog( TRect( 20, 6, 60, 16 ), "atui on OS/2" );
    TProgram::deskTop->insert( dialog );
    TProgram::deskTop->draw();

    TEvent event;
    app.getEvent( event );        // blocks until the driver reports a key
    return 0;
}
