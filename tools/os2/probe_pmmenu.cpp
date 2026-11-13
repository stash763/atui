// probe_pmmenu.cpp -- can the resource-loaded menu bar be edited at
// run time, and which handle does the editing?
//
// Why this is worth an afternoon: File > Sessions wants a list that
// changes -- the user's request is a menu that shows the sessions
// that exist, not a dialog that says "go look".  A menu built by
// WinLoadMenu is built from the image and called "initialized"; the
// PM 2.x record API (FCM_INSERTITEM and friends) is not in this
// Watcom's headers at all (measured: C:\WATCOM\H\OS2\PMWIN.H carries
// no FCM_FINDMENU and no FCMENURECORD), but the PM 1.x item messages
// survived in the same header (MM_SETITEMTEXT 0x018e, MM_INSERTITEM
// 0x0180, MM_DELETEITEM 0x0181, MM_SETITEMATTR 0x0192).  Warp kept
// them for compatibility; whether they reach a menu bar that WinLoad
// Menu produced is exactly the kind of thing to measure.
//
// The questions, in order:
//   E2  WinLoadMenu from atuipm.exe's own resources onto a hidden
//       frame (module passed as a path string) -- does it load?
//   E3  who answers MM_QUERYITEMTEXT for an item id: the frame? the
//       menu bar window?  (STRREC round trip, File > Help id 1101)
//   E4  the File submenu's handle: WinWindowFromID(menubar, 1100)?
//       FCM_FINDMENU guesses at WM_USER+0x205..0x207?  A guess that
//       returns a live handle answers E5; a wrong one answers nothing.
//   E5  on a handle that answers: set the text, read it back, restore
//       it; check and uncheck (MIA_CHECKED); insert an item
//       (MM_INSERTITEM with MENUITEM, MIT_LAST), validate it, give it
//       text, delete it.  Those five are the whole of the update the
//       sessions menu needs.
//
// Nothing is shown and no message loop is pumped: the probe creates a
// frame it never shows, talks to its own objects, destroys them and
// exits -- the grab-and-exit category that is safe from telnet
// (probe_pmcap's rules).  Answers go to C:\atui\pmmenu.txt.
#ifndef INCL_DOS
#define INCL_DOS
#endif
#ifndef INCL_PM
#define INCL_PM
#endif
#include <os2.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef MIT_LAST
#define MIT_LAST    (-3)
#endif
#ifndef MIS_TEXT
#define MIS_TEXT    0x0001
#endif
#ifndef MIA_CHECKED
#define MIA_CHECKED 0x2000
#endif
#ifndef MIA_DISABLED
#define MIA_DISABLED 0x4000
#endif
#ifndef WM_REALIZEWIN
#define WM_REALIZEWIN 0x0201      /* WM_USER + 1, the same since 1.x */
#endif

/* STRREC by hand, the way pmdlg.cpp carries it (the name is in no
   header this build reads). */
typedef struct _ATUISTRREC
    {
    USHORT usStrLength;
    PSZ    pszBuffer;
    } ATUISTRREC;

static FILE *g;

static void say( const char *fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    vfprintf( g, fmt, ap );
    fflush( g );
    va_end( ap );
}

/* The anchor's last error.  This Watcom declares the one-word form
   (measured: WinGetLastError(HAB) -> ULONG); pmhands read the same
   shape when it saw 0x81001. */
static void lasterr( HAB h )
{
    say( "      lasterr=%lx\n", (ULONG) WinGetLastError( h ) );
}

/* An item's text through the STRREC idiom, or 0.  (`Boolean` is not
   in the C++ header chain this probe compiles through; int answers.) */
static int qryText( HWND h, ULONG id, char *buf, USHORT cap )
{
    buf[ 0 ] = '\0';
    if( h == 0 || cap == 0 )
        return 0;
    const USHORT need = (USHORT)(ULONG) WinSendMsg( h,
        MM_QUERYITEMTEXTLENGTH, MPFROMLONG( id ), MPVOID );
    if( need == 0 || need == 0xFFFF || need + 1 > cap )
        return 0;
    ATUISTRREC rec;
    rec.usStrLength = cap - 1;
    rec.pszBuffer = (PSZ) buf;
    if( !WinSendMsg( h, MM_QUERYITEMTEXT, MPFROMLONG( id ),
                     MPFROMLONG( &rec ) ) )
        return 0;
    buf[ cap - 1 ] = '\0';
    return 1;
}

int main()
{
    g = fopen( "c:\\atui\\pmmenu.txt", "w" );
    if( g == 0 )
        return 1;

    say( "pmmenu probe built %s %s\n", __DATE__, __TIME__ );

    /* E1: the anchor -- and its queue.  WinCreateWindow with no queue
       in this Watcom answers PMERR_NO_MSG_QUEUE (measured 2026-11-12,
       lasterr 0x41036); atuipm's own birth log runs
       WinInitialize/WinCreateMsgQueue before the frame, and the probe
       follows the same order.  Nothing is pumped from the queue, it
       is existence the window manager checks. */
    HAB hab = WinInitialize( 0 );
    say( "E1 WinInitialize: %s\n", hab ? "ok" : "FAIL" );
    if( hab == 0 )
    {
        fclose( g );
        return 2;
    }
    HMQ hmq = WinCreateMsgQueue( hab, 0 );
    say( "E1 WinCreateMsgQueue: %s\n", hmq ? "ok" : "FAIL" );

    /* A VIO-started process gets a PM anchor but no desktop, and
       WinOpen -- the call WinMain makes to attach one -- is not an
       export of os2386.lib (measured: E2028 at link).  So there is no
       anchor trick for telnet: run this probe with `start`, which
       places it in the session that has a desktop, and read the file
       it leaves. */

    /* E2: a hidden frame and the shipping program's own menu.  Two
       birth routes: the bare WinCreateWindow on the system frame
       class, and -- if that fails -- atuipm's own recipe of a
       registered class through WinCreateStdWindow (the Watcom macro,
       the same nine arguments the driver passes).  From the telnet
       anchor the first route traps outright (measured 2026-11-12:
       process dies inside the call, after PMERR_NO_MSG_QUEUE taught
       it to get that far with a queue); whether the second fares any
       differently there, or anywhere, is what this run is for. */
    say( "E2 WinCreateWindow calling...\n" );
    HWND frame = WinCreateWindow( HWND_DESKTOP, WC_FRAME, "pmmenu",
                                  0L, 0, 0, 40, 40, HWND_DESKTOP,
                                  HWND_TOP, 99, NULL,
                                  (void *) (ULONG) FCF_TITLEBAR );
    say( "E2 WinCreateWindow frame: %p\n", (void *)frame );
    if( frame == 0 )
        {
        lasterr( hab );
        say( "E2 std route calling...\n" );
        WinRegisterClass( hab, "PmmenuApp", (PFNWP) WinDefWindowProc, 0, 0 );
        ULONG fl2 = FCF_TITLEBAR;
        HWND client2 = 0;
        frame = WinCreateStdWindow( HWND_DESKTOP, 0, &fl2, "PmmenuApp",
                                    "pmmenu", 0, (HMODULE) 0, 98,
                                    &client2 );
        say( "E2 WinCreateStdWindow frame: %p client: %p\n",
             (void *)frame, (void *)client2 );
        if( frame == 0 )
            lasterr( hab );
        }
    HWND mb = 0;
    if( frame )
        {
        /* Watcom's headers carry no WinRealizeWindow macro (measured);
           the call is that message. */
        WinSendMsg( frame, WM_REALIZEWIN, MPVOID, MPVOID );
        mb = WinLoadMenu( frame, (HMODULE)(PCH) "c:\\atui\\atuipm.exe",
                          256 );
        say( "E2 WinLoadMenu(atuipm.exe,256): %p\n", (void *)mb );
        if( mb == 0 )
            lasterr( hab );
        }

    /* E3: who answers for item ids? */
    char t[ 200 ];
    HWND fmenu = frame ? WinWindowFromID( frame, FID_MENU ) : 0;
    say( "E3 WinWindowFromID(frame,FID_MENU): %p\n", (void *)fmenu );
    say( "E3 frame: text(1101)=%s[%s]\n",
         qryText( frame, 1101, t, sizeof t ) ? "yes" : "no", t );
    say( "E3 fmenu: text(1101)=%s[%s]\n",
         qryText( fmenu, 1101, t, sizeof t ) ? "yes" : "no", t );
    say( "E3 mb:    text(1101)=%s[%s]\n",
         qryText( mb, 1101, t, sizeof t ) ? "yes" : "no", t );

    /* E4: the File submenu's handle, by every candidate route.  The
       FCM_FINDMENU guesses are arithmetic on WM_USER -- no header
       states them -- and a wrong number answers no question here
       because the candidate is only chosen when it answers a text
       query. */
    HWND byId  = mb ? WinWindowFromID( mb, 1100 ) : 0;
    HWND by205 = mb ? (HWND) WinSendMsg( mb, WM_USER + 0x205,
                            MPFROMLONG( 1100 ), MPVOID ) : 0;
    HWND by206 = mb ? (HWND) WinSendMsg( mb, WM_USER + 0x206,
                            MPFROMLONG( 1100 ), MPVOID ) : 0;
    HWND by207 = mb ? (HWND) WinSendMsg( mb, WM_USER + 0x207,
                            MPFROMLONG( 1100 ), MPVOID ) : 0;
    HWND fById = frame ? WinWindowFromID( frame, 1100 ) : 0;
    HWND f206  = frame ? (HWND) WinSendMsg( frame, WM_USER + 0x206,
                               MPFROMLONG( 1100 ), MPVOID ) : 0;
    HWND fFid  = fmenu;
    say( "E4 mb: fromID=%p FCM205=%p FCM206=%p FCM207=%p\n",
         (void *)byId, (void *)by205, (void *)by206, (void *)by207 );
    say( "E4 frame: fromID=%p FCM206=%p FID_MENU=%p\n",
         (void *)fById, (void *)f206, (void *)fFid );
    const HWND cand[ 7 ] = { byId, by205, by206, by207,
                             fById, f206, fFid };
    HWND hFile = 0;
    for( int i = 0; i < 7; ++i )
        {
        if( cand[ i ] == 0 )
            continue;
        if( qryText( cand[ i ], 1101, t, sizeof t ) )
            {
            say( "E4 cand[%d]=%p answers 1101 [%s]\n", i,
                 (void *)cand[ i ], t );
            if( hFile == 0 )
                hFile = cand[ i ];
            }
        }
    say( "E4 chosen: %p\n", (void *)hFile );

    if( hFile )
        {
        /* E5: the update dance the shipping code needs. */
        const LONG count = (LONG) WinSendMsg( hFile, MM_QUERYITEMCOUNT,
                                              MPVOID, MPVOID );
        say( "E5 count=%ld\n", count );
        say( "E5 valid(1140 pre-insert)=%ld\n",
             (long) WinSendMsg( hFile, MM_ISITEMVALID,
                                MPFROM2SHORT( 1140, TRUE ),
                                MPFROMLONG( FALSE ) ) );

        char saved[ 200 ];
        const int had = qryText( hFile, 1101, saved, sizeof saved );
        say( "E5 settext: %d\n",
             (int) WinSetMenuItemText( hFile, 1101, "Probe-text-X" ) );
        char back[ 200 ];
        qryText( hFile, 1101, back, sizeof back );
        say( "E5 readback [%s]\n", back );
        if( had )
            WinSetMenuItemText( hFile, 1101, saved );

        say( "E5 check: %d, checked=%ld\n",
             (int) WinCheckMenuItem( hFile, 1101, TRUE ),
             (long) WinIsMenuItemChecked( hFile, 1101 ) );
        WinCheckMenuItem( hFile, 1101, FALSE );
        /* WinDisableMenuItem is not among this Watcom's macros
           (measured); the attribute message is spelled out. */
        say( "E5 disable=%d disabled-attr=%ld enable=%ld\n",
             (int) WinSendMsg( hFile, MM_SETITEMATTR,
                               MPFROM2SHORT( 1101, TRUE ),
                               MPFROMLONG( MIA_DISABLED ) ),
             (long) WinSendMsg( hFile, MM_QUERYITEMATTR,
                                MPFROM2SHORT( 1101, TRUE ),
                                MPFROMLONG( MIA_DISABLED ) ),
             (long) WinEnableMenuItem( hFile, 1101, TRUE ) );

        /* The insertion.  No header on hand states the MP shape of
           MM_INSERTITEM (1.x called it through a macro that survived
           nowhere), and WinInsertMenuItem does not exist in this
           Watcom (measured).  Three plausible shapes, each answered
           by MM_ISITEMVALID; the first that validates wins and gets
           texted and deleted. */
        MENUITEM mi;
        memset( &mi, 0, sizeof mi );
        mi.iPosition = MIT_LAST;
        mi.afStyle = MIS_TEXT;
        mi.afAttribute = 0;
        mi.id = 1199;
        mi.hwndSubMenu = 0;
        mi.hItem = 0;

        const LONG pos1101 =
            (LONG) WinSendMsg( hFile, MM_ITEMPOSITIONFROMID,
                               MPFROMLONG( 1101 ), MPVOID );
        say( "E5 pos(1101)=%ld\n", pos1101 );

        LONG ins = (LONG) WinSendMsg( hFile, MM_INSERTITEM,
                                      MPFROMP( &mi ), MPVOID );
        int form = 'A';
        if( !WinSendMsg( hFile, MM_ISITEMVALID,
                         MPFROM2SHORT( 1199, TRUE ),
                         MPFROMLONG( FALSE ) ) )
        {
            ins = (LONG) WinSendMsg( hFile, MM_INSERTITEM,
                                     MPFROMLONG( 1199 ), MPFROMP( &mi ) );
            form = 'B';
        }
        if( !WinSendMsg( hFile, MM_ISITEMVALID,
                         MPFROM2SHORT( 1199, TRUE ),
                         MPFROMLONG( FALSE ) ) )
        {
            ins = (LONG) WinSendMsg( hFile, MM_INSERTITEM,
                                     MPFROM2SHORT( MIT_LAST, 1199 ),
                                     MPFROMP( &mi ) );
            form = 'C';
        }
        say( "E5 insert form=%c rc=%ld valid=%ld\n", form, ins,
             (long) WinSendMsg( hFile, MM_ISITEMVALID,
                                MPFROM2SHORT( 1199, TRUE ),
                                MPFROMLONG( FALSE ) ) );
        WinSetMenuItemText( hFile, 1199, "inserted-Y" );
        qryText( hFile, 1199, back, sizeof back );
        say( "E5 inserted text [%s]\n", back );
        say( "E5 delete=%ld valid=%ld count=%ld\n",
             (long) WinSendMsg( hFile, MM_DELETEITEM,
                                MPFROMLONG( 1199 ), MPVOID ),
             (long) WinSendMsg( hFile, MM_ISITEMVALID,
                                MPFROM2SHORT( 1199, TRUE ),
                                MPFROMLONG( FALSE ) ),
             (long) WinSendMsg( hFile, MM_QUERYITEMCOUNT,
                                MPVOID, MPVOID ) );
        }

    if( frame )
        WinDestroyWindow( frame );
    if( hmq )
        WinDestroyMsgQueue( hmq );
    WinTerminate( hab );
    say( "done\n" );
    fclose( g );
    return 0;
}
