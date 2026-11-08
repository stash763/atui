// OS/2 PM backend of the hands seam. See platform/appman.h.
//
// Every call in this file is one the pmdrive probe made against live
// targets and survived -- or one it made and did not, which is why no
// WM_CHAR lives here at all. The journal in TESTING.md carries the
// deaths; the shape of this file is what the survivors allowed.
//
// The session fact everything else bends around: a PM window belongs
// to a session, and only the session that owns the display can show
// one. DosStartSession from an anchored (telnet) session answers
// foreground requests with 457 and background ones with a window
// that exists, enumerates, and never paints to the visible screen --
// so `how` is not decoration; it is the tool telling the truth about
// what the user will and will not see.
#include "platform/appman.h"
#include "core/strutil.h"

#if defined( ATUI_HAVE_OS2_PM )

#define INCL_DOSPROCESS
#define INCL_DOSERRORS
#define INCL_WIN

#include <os2.h>

#include <cstring>
#include <string>

namespace atui {
namespace platform {

namespace {

// The anchor, once per process -- the same arrangement screen_os2.cpp
// uses and for the same documented reason: repeat WinInitialize calls
// hand back the anchor, they do not fight the driver's own.
HAB anchor()
{
    static HAB hab = WinInitialize( 0 );
    return hab;
}

// The notification standard PM buttons announce with WM_COMMAND.
// BN_CLICKED under IBM's names; the value survived a full round trip
// through a live window's owner procedure during the E probe.
const USHORT kNotifClick = 0x0100;

// The shared body of the window acts: a handle string, a live check,
// and a posted message. Posting, never sending: a send blocks this
// process until the target's thread answers, and a wedged third-party
// app must not be able to wedge the chat client with it.
bool postTo( const std::string &handle, ULONG msg, ULONG mp1, ULONG mp2,
             const char *what, std::string &error )
{
    unsigned long raw = 0;
    if ( !parseWindowHandle( handle, raw ) )
    {
        error = "not a window handle (list windows first)";
        return false;
    }
    const HAB hab = anchor();
    if ( hab == 0 )
    {
        error = "no PM anchor (WinInitialize failed)";
        return false;
    }
    const HWND hwnd = (HWND)raw;
    if ( !WinIsWindow( hab, hwnd ) )
    {
        error = "no such window (it may have closed since it was listed)";
        return false;
    }
    if ( !WinPostMsg( hwnd, msg, MPFROMLONG( mp1 ), MPFROMLONG( mp2 ) ) )
    {
        error = what;
        error += ": WinPostMsg failed";
        return false;
    }
    return true;
}

} // anonymous namespace

bool appLaunchAvailable()
{
    return true;
}

bool appPostAvailable()
{
    return true;
}

bool appLaunch( const std::string &program,
                std::string &how,
                unsigned long &pid,
                std::string &error )
{
    STARTDATA sd;
    std::memset( &sd, 0, sizeof sd );
    sd.Length = (USHORT)sizeof sd;
    sd.Related = SSF_RELATED_INDEPENDENT;
    // Watcom's header spells the no-pause option TRACEOPT_NONE: on this
    // toolkit tracing IS the pause, so "none" is what keeps a finished
    // child from holding the "Press any key" screen.
    sd.TraceOpt = SSF_TRACEOPT_NONE;
    sd.SessionType = SSF_TYPE_DEFAULT;
    sd.PgmTitle = (PSZ)"atui";
    sd.PgmName = (PSZ)program.c_str();

    ULONG status = 0;
    pid = 0;

    // Foreground first: the chat client runs in the session that owns
    // the display (it is on screen right now), so from here a
    // foreground start is what the user expects to see. The retry is
    // for anchors without a display -- and honesty about what a
    // background start means there is the whole point of `how`.
    sd.FgBg = SSF_FGBG_FORE;
    APIRET rc = DosStartSession( &sd, &status, (PPID)&pid );
    if ( rc == 0 )
    {
        how = "foreground";
        return true;
    }
    if ( rc != ERROR_SMG_START_IN_BACKGROUND )
    {
        error = "DosStartSession rc=" + atui::core::toString( (long)rc );
        return false;
    }

    sd.FgBg = SSF_FGBG_BACK;
    rc = DosStartSession( &sd, &status, (PPID)&pid );
    if ( rc != 0 )
    {
        error = "DosStartSession rc=" + atui::core::toString( (long)rc );
        return false;
    }
    // Measured: a background start from an anchor reports no pid, and
    // its windows -- while enumerable and closable -- never appear on
    // the visible screen. The tool text says so out loud.
    how = "background (this session does not own the display: the "
          "program runs but its window may not appear on screen)";
    return true;
}

bool appCloseWindow( const std::string &handle, std::string &error )
{
    return postTo( handle, WM_CLOSE, 0, 0, "close", error );
}

bool appClickWindow( const std::string &handle, std::string &error )
{
    unsigned long raw = 0;
    if ( !parseWindowHandle( handle, raw ) )
    {
        error = "not a window handle (list windows first)";
        return false;
    }
    const HAB hab = anchor();
    if ( hab == 0 )
    {
        error = "no PM anchor (WinInitialize failed)";
        return false;
    }
    const HWND hwnd = (HWND)raw;
    if ( !WinIsWindow( hab, hwnd ) )
    {
        error = "no such window (it may have closed since it was listed)";
        return false;
    }
    const USHORT id = WinQueryWindowUShort( hwnd, QWS_ID );
    if ( id == 0 )
    {
        error = "control has no command id (frames and self-driving "
                "windows take app_send_msg instead)";
        return false;
    }
    // The command belongs to the control's owner -- the dialog or page
    // that listens for its buttons -- exactly as the click would.
    HWND owner = WinQueryWindow( hwnd, QW_OWNER );
    if ( owner == 0 )
        owner = WinQueryWindow( hwnd, QW_PARENT );
    if ( owner == 0 )
    {
        error = "control has no owner to notify";
        return false;
    }
    if ( !WinPostMsg( owner, WM_COMMAND,
                      MPFROMLONG( (ULONG)id ),
                      MPFROM2SHORT( kNotifClick, 0 ) ) )
    {
        error = "click: WinPostMsg failed";
        return false;
    }
    return true;
}

bool appSetWindowText( const std::string &handle,
                       const std::string &text,
                       std::string &error )
{
    unsigned long raw = 0;
    if ( !parseWindowHandle( handle, raw ) )
    {
        error = "not a window handle (list windows first)";
        return false;
    }
    const HAB hab = anchor();
    if ( hab == 0 )
    {
        error = "no PM anchor (WinInitialize failed)";
        return false;
    }
    const HWND hwnd = (HWND)raw;
    if ( !WinIsWindow( hab, hwnd ) )
    {
        error = "no such window (it may have closed since it was listed)";
        return false;
    }
    // The marshaled system door: PM copies the string across address
    // spaces for WM_SETTEXT, which is more than this process may ever
    // do with raw control messages. SPIN controls are known to refuse
    // it; the refusal comes back as an error, not as silence.
    if ( !WinSetWindowText( hwnd, (PSZ)text.c_str() ) )
    {
        error = "set_text: WinSetWindowText refused (SPIN controls "
                "keep their own text)";
        return false;
    }
    return true;
}

bool appPostMessage( const std::string &handle,
                     unsigned long msg,
                     unsigned long mp1,
                     unsigned long mp2,
                     std::string &error )
{
    return postTo( handle, (ULONG)msg, mp1, mp2, "send_msg", error );
}

void appSleepMs( int ms )
{
    if ( ms < 0 )
        ms = 0;
    DosSleep( (ULONG)ms );
}

} // namespace atui::platform
} // namespace atui

#endif // ATUI_HAVE_OS2_PM
