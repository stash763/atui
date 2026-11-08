// The answer for platforms with no hands yet. See platform/appman.h.
//
// Every capability says no, every call fails with the same quiet
// sentence, and the registry then never tells the model it has hands
// on this machine. X11's answer (XTEST, which is the real device door
// there and will not have the OS/2 MLE story in its future) replaces
// this file's silence when someone builds it.
#include "platform/appman.h"

#if !defined( ATUI_HAVE_OS2_PM )

#if defined( __OS2__ )
#define INCL_DOSPROCESS
#include <os2.h>
#elif defined( _WIN32 )
#include <windows.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif

namespace atui {
namespace platform {

namespace {

const char *const kNoHands =
    "no application control is available on this platform";

} // anonymous namespace

bool appLaunchAvailable()
{
    return false;
}

bool appPostAvailable()
{
    return false;
}

bool appLaunch( const std::string &program,
                std::string &how,
                unsigned long &pid,
                std::string &error )
{
    ( void ) program; ( void ) how; ( void ) pid;
    error = kNoHands;
    return false;
}

bool appCloseWindow( const std::string &handle, std::string &error )
{
    ( void ) handle;
    error = kNoHands;
    return false;
}

bool appClickWindow( const std::string &handle, std::string &error )
{
    ( void ) handle;
    error = kNoHands;
    return false;
}

bool appSetWindowText( const std::string &handle,
                       const std::string &text,
                       std::string &error )
{
    ( void ) handle; ( void ) text;
    error = kNoHands;
    return false;
}

bool appPostMessage( const std::string &handle,
                     unsigned long msg,
                     unsigned long mp1,
                     unsigned long mp2,
                     std::string &error )
{
    ( void ) handle; ( void ) msg; ( void ) mp1; ( void ) mp2;
    error = kNoHands;
    return false;
}

void appSleepMs( int ms )
{
    if ( ms < 0 )
        ms = 0;
#if defined( __OS2__ )
    DosSleep( (ULONG)ms );
#elif defined( _WIN32 )
    Sleep( (DWORD)ms );
#else
    // select() on no fds: portable to the C99s this tree meets, and
    // it takes milliseconds without the one-second granularity of
    // sleep().
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = ( ms % 1000 ) * 1000;
    select( 0, 0, 0, 0, &tv );
#endif
}

} // namespace atui::platform
} // namespace atui

#endif // !ATUI_HAVE_OS2_PM
