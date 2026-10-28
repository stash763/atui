/* pmhello.cpp -- PM scaffold probe: a minimal framed window, built the way
   Watcom 1.9 + eFTE do it.

   Never start the resulting exe from a telnet session: a PM program has no
   screen there and hangs the session.  pmhello.cmd only compiles and links;
   the window itself must be started at the box's own display.  What it
   proves when it runs:
     1. WinInitialize/WinCreateMsgQueue/WinGetMsg: a PM process lives;
     2. WinCreateStdWindow + WM_PAINT: it paints (blue field, text via
        GpiCharStringAt);
     3. WM_CHAR: the keyboard reaches it -- q or Esc closes it;
     4. clean shutdown (WM_DESTROY -> WM_QUIT -> loop exit -> WinTerminate),
        each step appended to pmhello.run.txt so the host can confirm the
        cycles over FTP even unattended.

   Lesson from the first run: a frameless WinCreateWindow directly off
   HWND_DESKTOP never receives focus, so WM_CHAR never arrived -- it had to
   be killed.  A frame (titlebar, sysmenu, min/max) is the cure and the
   advice; q/Esc post WM_CLOSE to the frame as a backstop.  The frame recipe
   is the toolkit template's (~/code/TOOLKIT/samples/pm/template):
   FCF_STANDARD, WS_VISIBLE child, CS_SIZEREDRAW | CS_CLIPCHILDREN.

   API names follow the Watcom headers and eFTE's working calls: queue is
   WinCreateMsgQueue/WinDestroyMsgQueue (the Warp "Open/Close" spellings are
   not declared), key decoding is SHORT1FROMMP(mp2)=char, CHAR4FROMMP(mp1)
   =scan code (g_pm.cpp:2616).

   Throwaway by design: answers belong in TESTING.md, files go away.
*/
#define INCL_DOSERRORS
#define INCL_GPI
#define INCL_WIN
#include <os2.h>

#include <stdio.h>

static FILE *logf;

static void plog(const char *what)
{
    if( logf != 0 )
    {
        fprintf( logf, "%s\n", what );
        fflush( logf );
    }
}

static MRESULT EXPENTRY probeWndProc( HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
    switch( msg )
    {
    case WM_PAINT:
        {
            RECTL   rcl;
            POINTL  ptl;
            HPS     hps = WinBeginPaint( hwnd, 0, &rcl );

            WinFillRect( hps, &rcl, CLR_BLUE );
            GpiSetColor( hps, CLR_YELLOW );

            ptl.x = 20; ptl.y = 30;
            GpiCharStringAt( hps, &ptl, 26, "atui PM scaffold works" );
            ptl.y = 14;
            GpiCharStringAt( hps, &ptl, 40, "press q or Esc to close this window" );

            WinEndPaint( hps );
            plog( "paint ok" );
        }
        return 0;

    case WM_CHAR:
        plog( "char received" );
        if( SHORT1FROMMP( mp2 ) == 'q' || SHORT1FROMMP( mp2 ) == 0x1b )
            WinPostMsg( WinQueryWindow( hwnd, QW_PARENT ), WM_CLOSE, 0, 0 );
        return 0;

    case WM_DESTROY:
        WinPostMsg( HWND_DESKTOP, WM_QUIT, 0, 0 );
        return 0;
    }
    return WinDefWindowProc( hwnd, msg, mp1, mp2 );
}

int main()
{
    HAB     hab;
    HMQ     hmq;
    HWND    hwndFrame;
    HWND    hwndClient;
    QMSG    qmsg;
    ULONG   flFrame = FCF_STANDARD;   /* the toolkit template's frame: titlebar,
                                         sysmenu, sizeborder, min/max */

    logf = fopen( "pmhello.run.txt", "w" );
    plog( "start" );

    hab = WinInitialize( 0 );
    if( hab == 0 )
    {
        plog( "WinInitialize failed" );
        return 1;
    }
    plog( "hab ok" );

    hmq = WinCreateMsgQueue( hab, 0 );
    if( hmq == 0 )
    {
        plog( "WinCreateMsgQueue failed" );
        WinTerminate( hab );
        return 1;
    }
    plog( "hmq ok" );

    if( !WinRegisterClass( hab, "AtuiProbe", (PFNWP) probeWndProc,
                           CS_SIZEREDRAW | CS_CLIPCHILDREN, 0 ) )
    {
        plog( "WinRegisterClass failed" );
        WinDestroyMsgQueue( hmq );
        WinTerminate( hab );
        return 1;
    }

    hwndFrame = WinCreateStdWindow( HWND_DESKTOP, WS_VISIBLE, &flFrame,
                                    "AtuiProbe", "atui PM probe",
                                    WS_VISIBLE, 0, 0, &hwndClient );
    if( hwndFrame == 0 )
    {
        plog( "WinCreateStdWindow failed" );
        WinDestroyMsgQueue( hmq );
        WinTerminate( hab );
        return 1;
    }
    WinSetFocus( HWND_DESKTOP, hwndClient );
    plog( "framed window created+shown, focus set" );

    while( WinGetMsg( hab, &qmsg, NULLHANDLE, 0, 0 ) )
        WinDispatchMsg( hab, &qmsg );

    plog( "loop exited" );
    WinDestroyMsgQueue( hmq );
    WinTerminate( hab );
    plog( "done" );
    return 0;
}
