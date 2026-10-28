/* pmhello.cpp -- PM scaffold probe: a twin of the toolkit stdwnd sample
   (~/code/TOOLKIT/samples/pm/stdwnd/hello.c, also Watcom-tested at
   open-watcom-v2 bld/wv/os2/pmtest), built against pmhello.rc resources.

   Never start the resulting exe from a telnet session: a PM program has no
   screen there and hangs the session.  pmhello.cmd compiles, links and
   appends the resources; the window itself must be started at the box's own
   display.

   Why this rewrite: the previous frameless variant painted but never got
   focus (killed by hand), and its successor failed WinCreateStdWindow with
   PMERR_RESOURCE_NOT_FOUND (0x8100a) -- a Watcom system os2v2_pm image has
   no resource segment until rc appends one, which is exactly what both the
   toolkit mak (rc -r ftepm.rc) and Watcom's own pmtest (rc -p -x hello.res
   hello.exe) do after linking.  So: hello.c's exact call sequence --

     flCreate = FCF_STANDARD & ~FCF_SHELLPOSITION
     WinCreateStdWindow(desktop, 0, &flCreate, class, title, 0, 0,
                        ID_WINDOW, &hwndClient)          (idResources = 256)
     WinSetWindowPos(frame, HWND_TOP, x, y, w, h,
                     SWP_SIZE | SWP_MOVE | SWP_ACTIVATE | SWP_SHOW)

   plus WM_ERASEBACKGROUND -> TRUE, WM_CLOSE -> WM_QUIT, and
   WinDestroyWindow after the loop, as the sample does.  q/Esc/F3/Exit all
   close; each milestone lands in pmhello.run.txt.

   API names follow the Watcom headers and eFTE's working calls: queue is
   WinCreateMsgQueue/WinDestroyMsgQueue, key decoding is
   SHORT1FROMMP(mp2)=char, CHAR4FROMMP(mp1)=scan code (g_pm.cpp:2616).

   Throwaway by design: answers belong in TESTING.md, files go away.
*/
#define INCL_DOSERRORS
#define INCL_GPI
#define INCL_WIN
#include <os2.h>

#include <stdio.h>

#include "pmhello.h"

static FILE *logf;
static HAB   hab;

static void plog(const char *what)
{
    if( logf != 0 )
    {
        fprintf( logf, "%s\n", what );
        fflush( logf );
    }
}

/* Log the PM error code and its human text: WinGetErrorInfo's block holds
   an array of string offsets at offaoffszMsg -- the first entry is the
   message (the unpack follows hello.c's AbortHello). */
static void plogerr( const char *what )
{
    if( logf != 0 )
    {
        PERRINFO ei;
        fprintf( logf, "%s: error 0x%lx\n", what, WinGetLastError( hab ) );
        ei = WinGetErrorInfo( hab );
        if( ei != 0 )
        {
            PSZ pszOffSet = (PSZ) ei + ei->offaoffszMsg;
            PSZ pszMsg    = (PSZ) ei + *(PUSHORT) pszOffSet;
            fprintf( logf, "  info: %s\n", pszMsg );
            WinFreeErrorInfo( ei );
        }
        fflush( logf );
    }
}

static MRESULT EXPENTRY probeWndProc( HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
    switch( msg )
    {
    case WM_ERASEBACKGROUND:
        return (MRESULT) TRUE;        /* let PM paint SYSCLR_WINDOW first */

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

    case WM_COMMAND:
        if( SHORT1FROMMP( mp1 ) == ID_EXITPROG )   /* menu Exit / F3 */
        {
            plog( "menu exit" );
            WinPostMsg( hwnd, WM_CLOSE, 0, 0 );
            return 0;
        }
        break;

    case WM_CLOSE:
        plog( "close -> quit" );
        WinPostMsg( hwnd, WM_QUIT, 0, 0 );         /* hello.c's exit path */
        return 0;

    case WM_DESTROY:
        WinPostMsg( HWND_DESKTOP, WM_QUIT, 0, 0 ); /* backstop */
        return 0;
    }
    return WinDefWindowProc( hwnd, msg, mp1, mp2 );
}

int main()
{
    HMQ     hmq;
    HWND    hwndFrame = 0;
    HWND    hwndClient = 0;
    QMSG    qmsg;
    ULONG   flCreate = FCF_STANDARD & ~FCF_SHELLPOSITION;

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
        plogerr( "WinCreateMsgQueue failed" );
        WinTerminate( hab );
        return 1;
    }
    plog( "hmq ok" );

    if( !WinRegisterClass( hab, "AtuiProbe", (PFNWP) probeWndProc,
                           CS_SIZEREDRAW, 0 ) )
    {
        plogerr( "WinRegisterClass failed" );
        WinDestroyMsgQueue( hmq );
        WinTerminate( hab );
        return 1;
    }

    hwndFrame = WinCreateStdWindow( HWND_DESKTOP, 0, &flCreate,
                                    "AtuiProbe", "atui PM probe",
                                    0, (HMODULE) 0, ID_WINDOW, &hwndClient );
    if( hwndFrame == 0 )
    {
        plogerr( "WinCreateStdWindow failed" );
        WinDestroyMsgQueue( hmq );
        WinTerminate( hab );
        return 1;
    }
    plog( "std window created" );

    if( !WinSetWindowPos( hwndFrame, HWND_TOP, 50, 50, 480, 280,
                          SWP_SIZE | SWP_MOVE | SWP_ACTIVATE | SWP_SHOW ) )
    {
        plogerr( "WinSetWindowPos failed" );
        WinDestroyWindow( hwndFrame );
        WinDestroyMsgQueue( hmq );
        WinTerminate( hab );
        return 1;
    }
    plog( "window shown" );

    while( WinGetMsg( hab, &qmsg, NULLHANDLE, 0, 0 ) )
        WinDispatchMsg( hab, &qmsg );

    plog( "loop exited" );
    WinDestroyWindow( hwndFrame );
    WinDestroyMsgQueue( hmq );
    WinTerminate( hab );
    plog( "done" );
    return 0;
}
