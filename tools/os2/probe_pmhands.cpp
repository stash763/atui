// PM hands probe: the questions the application-control backend asks,
// put to the live box before any of them gets a shipping implementation.
//
// What this settles, one line of output per question:
//
//   * Which calls an anchor from a telnet session may make and which it
//     may not -- measured 2026-11-07: from an IFS session
//     WinInitialize/queries/capture all work but WinCreateWindow fails
//     (lasterr 0x81001).  So the arena tests below answer fully only at
//     the box's own display, while the launch test runs from either:
//     DosStartSession is a session-spawn API, not a window call, and
//     `start` from telnet has always spawned (onto some desktop).
//   * Does the id handed to WinCreateWindow come back from
//     WinQueryWindowUShort(hwnd, QWS_ID) -- the click tool identifies a
//     control exactly this way, and both headers agree the constant is
//     -1 without proving the round-trip.
//   * Does WM_CHAR arrive when SENT (WinSendMsg executes the target's
//     window procedure synchronously, across processes) and when
//     POSTED (it waits in the queue for the target's own loop, which is
//     the shape typed input actually has)?
//   * Does a WM_COMMAND posted to the owner window arrive with the
//     control id in SHORT1FROMMP(mp1) -- the shape every toolkit app's
//     command handler reads -- and what lands in SHORT2FROMMP's
//     notification slot when the sender fills 0x0100, the CN_SELECTED
//     value floating in documentation that no header on hand defines?
//     (The probe is the header.)
//   * MLE text replacement -- MLM_DELETE over a generous range plus
//     MLM_INSERT, the pair the PM driver already ships -- measured on
//     the probe's own MLE before pointed at anyone else's.  (There is
//     no MLM_SETTEXT: Watcom's list and IBM's own pmmle.h agree by
//     omission.)
//   * DosStartSession, with a zero-initialized STARTDATA save for
//     name and title: does a PM program launched this way appear, does
//     its window carry the returned PID into WinQueryWindowProcess (the
//     correlation the launch tool returns), and does WM_CLOSE take it
//     away again?  Not WinStartApp: Watcom's import library carries
//     only that one name (WinStartApp2 is neither declared nor
//     imported), and the 1.x documentation around it belongs to the
//     Win-OS/2 world -- an API whose current shape the headers cannot
//     even state is an API to leave alone.  DosStartSession is fully
//     declared and returns the PID, which is the thing the tool needs
//     anyway.
//   * Cross-process handles: is a window enumerated from another
//     process still a valid handle for queries here (WinIsWindow) and
//     for reads of its text -- read-only against a foreign window, this
//     probe writes nothing it does not own.
//
// PM rule: run at the box's own display for the full answers (it shows
// windows and pumps messages).  From telnet it still answers the launch
// question and prints, honestly, where the arena could not be built.
// Its answers land in C:\atui\hands_out.txt.
#ifndef INCL_DOS
#define INCL_DOS
#endif
#define INCL_WIN

#include <os2.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

FILE *g_out = 0;
HAB g_hab = 0;

void say( const char *fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    vfprintf( g_out, fmt, ap );
    va_end( ap );
    std::fflush( g_out );
}

const char *yesno( bool b ) { return b ? "ok" : "FAIL"; }

// Drain the message queue for a while -- posted messages and paints
// only go anywhere while somebody reads the queue.
void pumpMs( unsigned ms )
{
    for( unsigned waited = 0; waited < ms; waited += 50 )
    {
        QMSG qm;
        while( WinPeekMsg( g_hab, &qm, 0, 0, 0, PM_REMOVE ) )
            WinDispatchMsg( g_hab, &qm );
        DosSleep( 50 );
    }
}

// The kid window: records what input it receives, which is half of
// what the probe exists to learn.
MRESULT EXPENTRY KidProc( HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
    switch( msg )
    {
    case WM_CHAR:
        say( "kid: WM_CHAR arrived ch='%c' mp2lo=%x mp1=%x\n",
             (char)SHORT1FROMMP( mp2 ), (unsigned)SHORT1FROMMP( mp2 ),
             (unsigned)SHORT1FROMMP( mp1 ) );
        return 0;
    default:
        break;
    }
    return WinDefWindowProc( hwnd, msg, mp1, mp2 );
}

// The frame: the owner, listening for WM_COMMAND -- the exact shape a
// toolkit application's command handler consumes.
MRESULT EXPENTRY FrameProc( HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
    switch( msg )
    {
    case WM_COMMAND:
        say( "frame: WM_COMMAND id=%x notif=%x frameflags=%x\n",
             (unsigned)SHORT1FROMMP( mp1 ), (unsigned)SHORT1FROMMP( mp2 ),
             (unsigned)SHORT2FROMMP( mp2 ) );
        return 0;
    default:
        break;
    }
    return WinDefWindowProc( hwnd, msg, mp1, mp2 );
}

// The top-level windows' handles, for before/after diffing around the
// launch test.
struct TopList
{
    HWND h[ 64 ];
    int n;
};

void snapshotTops( TopList &t )
{
    t.n = 0;
    HWND w = WinQueryWindow( HWND_DESKTOP, QW_TOP );
    for( int i = 0; w != 0 && i < 64; ++i )
    {
        t.h[ t.n++ ] = w;
        w = WinQueryWindow( w, QW_NEXT );
    }
}

bool isTopped( const TopList &t, HWND h )
{
    for( int i = 0; i < t.n; ++i )
        if( t.h[ i ] == h )
            return true;
    return false;
}

void describeWindow( const char *tag, HWND hwnd )
{
    CHAR cls[ 64 ];
    cls[ 0 ] = '\0';
    WinQueryClassName( hwnd, (LONG)sizeof cls - 1, cls );
    CHAR title[ 96 ];
    title[ 0 ] = '\0';
    if( WinQueryWindowTextLength( hwnd ) > 0 )
        WinQueryWindowText( hwnd, (LONG)sizeof title - 1, title );
    SWP swp;
    WinQueryWindowPos( hwnd, &swp );
    PID pid = 0;
    TID tid = 0;
    WinQueryWindowProcess( hwnd, &pid, &tid );
    say( "%s h=%p class='%s' title='%s' x=%ld y=%ld %ldx%ld vis=%d "
         "pid=%lu valid=%d\n",
         tag, (void *)hwnd, cls, title, (long)swp.x, (long)swp.y,
         (long)swp.cx, (long)swp.cy, (int)WinIsWindowVisible( hwnd ),
         (unsigned long)pid, (int)WinIsWindow( g_hab, hwnd ) );
}

// The arena: own windows, own controls, every injection measured
// against them.  Runs only where WinCreateWindow answers.
HWND buildArena()
{
    if( !WinRegisterClass( g_hab, "ATUIHKID", KidProc, 0, 0 )
        || !WinRegisterClass( g_hab, "ATUIHPARENT", FrameProc,
                              CS_SIZEREDRAW, 0 ) )
    {
        say( "arena: WinRegisterClass FAIL lasterr=%lx\n",
             (unsigned long)WinGetLastError( g_hab ) );
        return 0;
    }
    // Watcom's WinCreateWindow carries no flFrameFlags argument (the
    // 2.x-era shape), so this window comes up frameless -- enough: it
    // must exist, enumerate and pump, not have a system menu.
    HWND frame = WinCreateWindow( HWND_DESKTOP, "ATUIHPARENT", "PMHANDS",
                                  WS_VISIBLE, 40, 40, 420, 300,
                                  0, 0, 1, NULL, NULL );
    say( "arena: frame WinCreateWindow %s lasterr=%lx\n",
         yesno( frame != 0 ), (unsigned long)WinGetLastError( g_hab ) );
    if( frame == 0 )
        say( "arena: the IFS answer from 2026-11-07 -- run at the "
             "display shell for these answers\n" );
    return frame;
}

void arenaTests( HWND frame )
{
    HWND kid1 = WinCreateWindow( frame, "ATUIHKID", "kid-one",
                                 WS_VISIBLE, 10, 200, 180, 60,
                                 frame, 0, 0x1234, NULL, NULL );
    HWND kid2 = WinCreateWindow( frame, "ATUIHKID", "kid-two",
                                 WS_VISIBLE, 200, 200, 180, 60,
                                 frame, 0, 0x5678, NULL, NULL );
    HWND mle = WinCreateWindow( frame, WC_MLE, "",
                                WS_VISIBLE, 10, 10, 400, 170,
                                frame, 0, 0x4321, NULL, NULL );
    say( "arena: kids %s %s mle %s\n", yesno( kid1 != 0 ),
         yesno( kid2 != 0 ), yesno( mle != 0 ) );
    describeWindow( "frame", frame );
    say( "frame: visible on YOUR screen? (the VD question -- a window\n"
         "  that enumerates fine can still live on another desktop)\n" );

    pumpMs( 600 );   // paints and any wake-up messages

    // ---- QWS_ID round-trip ------------------------------------------
    if( kid1 != 0 && kid2 != 0 )
        say( "QWS_ID kid1=%x (asked 0x1234) kid2=%x (asked 0x5678)\n",
             (unsigned)WinQueryWindowUShort( kid1, QWS_ID ),
             (unsigned)WinQueryWindowUShort( kid2, QWS_ID ) );

    // ---- child walk (the enumeration path the tools ship) ------------
    {
        int kids = 0;
        HWND c = WinQueryWindow( frame, QW_TOP );
        for( int i = 0; c != 0 && i < 16; ++i )
        {
            CHAR cls[ 48 ];
            cls[ 0 ] = '\0';
            WinQueryClassName( c, (LONG)sizeof cls - 1, cls );
            cls[ sizeof cls - 1 ] = '\0';
            say( "child %d: h=%p class='%s' id=%x\n", kids, (void *)c, cls,
                 (unsigned)WinQueryWindowUShort( c, QWS_ID ) );
            ++kids;
            c = WinQueryWindow( c, QW_NEXT );
        }
        say( "child walk: %d window(s)\n", kids );
    }

    // ---- WM_CHAR sent and posted -------------------------------------
    if( kid1 != 0 )
    {
        say( "sending WM_CHAR 'S' (WinSendMsg -- executes kid proc now)\n" );
        WinSendMsg( kid1, WM_CHAR, MPFROMLONG( 0 ), MPFROMLONG( 'S' ) );
        say( "posting WM_CHAR 'P' (arrives only through the pump)\n" );
        WinPostMsg( kid1, WM_CHAR, MPFROMLONG( 0 ), MPFROMLONG( 'P' ) );
        pumpMs( 300 );
    }

    // ---- WM_COMMAND to the owner --------------------------------------
    say( "posting WM_COMMAND id=0x1234 notif=0x0100 to owner\n" );
    WinPostMsg( frame, WM_COMMAND, MPFROMLONG( 0x1234 ),
                MPFROM2SHORT( 0x0100, 0 ) );
    pumpMs( 300 );

    // ---- WinSetWindowText round-trip ----------------------------------
    if( kid2 != 0 )
    {
        BOOL swr = WinSetWindowText( kid2, "TEXT-SET-OK" );
        CHAR buf[ 32 ];
        buf[ 0 ] = '\0';
        LONG got = WinQueryWindowText( kid2, (LONG)sizeof buf - 1, buf );
        say( "WinSetWindowText %s, query returned %ld text='%s'\n",
             yesno( swr ), got, buf );
    }

    // ---- MLE text replace (the set_text path for prompts) --------------
    if( mle != 0 )
    {
        WinSendMsg( mle, MLM_DELETE, MPFROMLONG( 0 ), MPFROMLONG( 65536 ) );
        MRESULT ir = WinSendMsg( mle, MLM_INSERT, MPFROMP( "MLE-SET-OK" ),
                                 MPVOID );
        MRESULT qm = WinSendMsg( mle, MLM_QUERYTEXTLENGTH, MPFROMLONG( 0 ),
                                 MPFROMLONG( 0 ) );
        say( "MLM_DELETE+INSERT rc=%ld, QUERYTEXTLENGTH rc=%ld (asked 9)\n",
             (long)LONGFROMMP( ir ), (long)LONGFROMMP( qm ) );
        pumpMs( 200 );   // let it paint -- visible proof on screen
    }
}

// Cross-process read: atuipm, if it is running.  Reads only -- this
// probe writes nothing in a window it does not own.
void crossProcessRead()
{
    HWND w = WinQueryWindow( HWND_DESKTOP, QW_TOP );
    HWND other = 0;
    for( int i = 0; w != 0 && i < 48 && other == 0; ++i )
    {
        CHAR cls[ 64 ];
        cls[ 0 ] = '\0';
        WinQueryClassName( w, (LONG)sizeof cls - 1, cls );
        if( std::strcmp( cls, "AtuiApp" ) == 0 )
            other = w;
        w = WinQueryWindow( w, QW_NEXT );
    }
    if( other == 0 )
    {
        say( "cross-process: no AtuiApp window to read -- start "
             "atuipm and rerun to settle this one\n" );
        return;
    }
    say( "cross-process: found atuipm client h=%p valid=%d\n",
         (void *)other, (int)WinIsWindow( g_hab, other ) );
    MRESULT ml = 0;
    HWND c = WinQueryWindow( other, QW_TOP );
    for( int i = 0; c != 0 && i < 8 && LONGFROMMP( ml ) == 0; ++i )
    {
        ml = WinSendMsg( c, MLM_QUERYTEXTLENGTH, MPFROMLONG( 0 ),
                         MPFROMLONG( 0 ) );
        c = WinQueryWindow( c, QW_NEXT );
    }
    say( "cross-process: MLM_QUERYTEXTLENGTH on its child = %ld "
         "(zero may mean MLE-with-no-text or no-answer; either way "
         "the handle itself answered)\n", (long)LONGFROMMP( ml ) );
}

void launchTest( const char *prog )
{
    // A plain read-mode open is the answer to the only question here
    // -- is there such a program to launch.  (Watcom 1.9's headers
    // predate DosQueryPathInfo.)
    FILE *probeFile = std::fopen( prog, "rb" );
    if( probeFile == 0 )
    {
        say( "launch: no %s to launch -- skipping\n", prog );
        return;
    }
    std::fclose( probeFile );

    // Flag settings to try in order -- first rc=0 wins, and the winner
    // is what the tool ships with.  Zero everywhere was the first
    // guess and answered 457 (ERROR_SMG_START_IN_BACKGROUND) from a
    // telnet context, so the field meanings get measured, not assumed.
    // The SSF_* names come from Watcom's own bsedos.h (grep had gone
    // hunting for the 1.x SSG_ spelling and found nothing; the probe
    // redefining one of them is how that was caught).
    APIRET rc = 457;
    ULONG agent = 0;
    PID pid = 0;
    for( int t = 0; t < 4 && rc != 0; ++t )
    {
        STARTDATA sd;
        std::memset( &sd, 0, sizeof sd );
        sd.Length = (USHORT)sizeof sd;
        sd.Related = SSF_RELATED_INDEPENDENT;
        sd.FgBg = SSF_FGBG_FORE;
        sd.InheritOpt = 0;
        sd.SessionType = SSF_TYPE_PM;
        const char *tag = "pm";
        if( t == 1 )
        {
            sd.InheritOpt = SSF_INHERTOPT_PARENT;
            tag = "pm+inherit";
        }
        else if( t == 2 )
        {
            sd.InheritOpt = SSF_INHERTOPT_PARENT;
            sd.SessionType = SSF_TYPE_DEFAULT;
            tag = "default+inherit";
        }
        else if( t == 3 )
        {
            sd.FgBg = SSF_FGBG_BACK;
            tag = "pm/background";
        }
        sd.PgmTitle = (PSZ)"PMHANDS PROBE";
        sd.PgmName = (PSZ)prog;
        rc = DosStartSession( &sd, &agent, &pid );
        say( "DosStartSession %s rc=%ld agent=%ld pid=%lu\n",
             tag, (long)rc, (long)agent, (unsigned long)pid );
    }
    if( rc != 0 )
        return;

    HWND appeared = 0;
    for( int t = 0; t < 12 && appeared == 0; ++t )
    {
        DosSleep( 500 );
        pumpMs( 50 );
        HWND w = WinQueryWindow( HWND_DESKTOP, QW_TOP );
        for( int i = 0; w != 0 && i < 64; ++i )
        {
            PID wpid = 0;
            TID wtid = 0;
            WinQueryWindowProcess( w, &wpid, &wtid );
            if( wpid == pid )
            {
                appeared = w;
                break;
            }
            w = WinQueryWindow( w, QW_NEXT );
        }
    }
    if( appeared == 0 )
    {
        say( "launch: pid=%lu has NO enumerable window (a VIO program "
             "or an early exit -- or a different virtual desktop, if "
             "you do not see it either)\n", (unsigned long)pid );
        return;
    }
    describeWindow( "launched:", appeared );
    say( "launch: found BY PID.  Did it appear on THIS screen?\n" );
    say( "  (found but unseen means another virtual desktop -- the\n"
         "   answer decides how the launch tool talks about what it\n"
         "   started)\n" );
    DosSleep( 3000 );   // give the eye a beat
    say( "closing it with WM_CLOSE\n" );
    WinPostMsg( appeared, WM_CLOSE, MPVOID, MPVOID );
    bool gone = false;
    for( int t = 0; t < 10 && !gone; ++t )
    {
        DosSleep( 300 );
        gone = !WinIsWindow( g_hab, appeared );
    }
    say( "WM_CLOSE: window %s\n", gone ? "gone" : "STILL THERE" );
}

} // anonymous namespace

int main( int argc, char **argv )
{
    g_out = std::fopen( "c:\\atui\\hands_out.txt", "w" );
    if( g_out == 0 )
        return 1;
    say( "pmhands probe: start\n" );

    g_hab = WinInitialize( 0 );
    say( "WinInitialize %s\n", yesno( g_hab != 0 ) );
    if( g_hab == 0 )
    {
        say( "no anchor -- run at the box's own display\n" );
        std::fclose( g_out );
        return 2;
    }

    const HWND frame = buildArena();
    if( frame != 0 )
        arenaTests( frame );
    crossProcessRead();

    char progBuf[ 256 ];
    const char *prog = "c:\\os2\\clocksprg.exe";
    if( argc > 1 && argv[ 1 ][ 0 ] != '\0' )
    {
        std::strncpy( progBuf, argv[ 1 ], sizeof progBuf - 1 );
        progBuf[ sizeof progBuf - 1 ] = '\0';
        prog = progBuf;
    }
    launchTest( prog );

    say( "done\n" );
    if( frame != 0 )
        WinDestroyWindow( frame );   // children go with it
    std::fclose( g_out );
    WinTerminate( g_hab );
    return 0;
}
