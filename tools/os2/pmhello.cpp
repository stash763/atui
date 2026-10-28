/* pmhello.cpp -- PM scaffold probe: a minimal real window, built the way
   Watcom 1.9 + eFTE do it.

   Never start the resulting exe from a telnet session: a PM program has no
   screen there and hangs the session.  pmhello.cmd only compiles and links;
   the window itself must be started at the box's own display.  What it
   proves when it runs:
     1. WinInitialize/WinCreateMsgQueue/WinGetMsg: a PM process lives;
     2. WinRegisterClass + WinCreateWindow + WM_PAINT: it paints (dark blue
        field, a few lines of text via GpiCharStringAt);
     3. WM_CHAR: the keyboard reaches it -- q or Esc closes it;
     4. clean shutdown (WinPostMsg desktop WM_QUIT -> loop exit ->
        WinTerminate), with each step appended to pmhello.run.txt so the
        host can confirm the paint cycle over FTP even unattended.

   API names follow the Watcom headers and eFTE's working calls: queue is
   WinCreateMsgQueue/WinDestroyMsgQueue (the Warp "Open/Close" spellings are
   not declared), key decoding is SHORT1FROMMP(mp2)=char,
   SHORT2FROMMP(mp2)=SC_*, CHAR4FROMMP(mp1)=scan code (g_pm.cpp:2616).

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
            WinPostMsg( hwnd, WM_CLOSE, 0, 0 );
        return 0;

    case WM_CLOSE:
        WinDestroyWindow( hwnd );
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
    HWND    hwnd;
    QMSG    qmsg;

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

    if( !WinRegisterClass( hab, "AtuiProbe", (PFNWP) probeWndProc, CS_SIZEREDRAW, 0 ) )
    {
        plog( "WinRegisterClass failed" );
        WinDestroyMsgQueue( hmq );
        WinTerminate( hab );
        return 1;
    }

    hwnd = WinCreateWindow( HWND_DESKTOP, "AtuiProbe", "atui PM probe",
                            WS_VISIBLE, 50, 50, 480, 280,
                            HWND_DESKTOP, HWND_TOP, 0, 0, 0 );
    if( hwnd == 0 )
    {
        plog( "WinCreateWindow failed" );
        WinDestroyMsgQueue( hmq );
        WinTerminate( hab );
        return 1;
    }
    plog( "window created+shown" );

    while( WinGetMsg( hab, &qmsg, NULLHANDLE, 0, 0 ) )
        WinDispatchMsg( hab, &qmsg );

    plog( "loop exited" );
    WinDestroyMsgQueue( hmq );
    WinTerminate( hab );
    plog( "done" );
    return 0;
}
