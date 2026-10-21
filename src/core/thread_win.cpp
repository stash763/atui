// Windows threads: `_beginthreadex` for the thread and the thread handle for the
// join, which is the pair that keeps the C runtime happy.
//
// Not run on a Windows machine yet, and said plainly rather than implied: the shape is
// the documented one (`CreateThread` would create a thread whose C runtime state is
// missing, which is the same trap as `DosCreateThread` on OS/2 and for the same
// reason), and it compiles, but the platform-specific behaviour this file is about,
// whether `_beginthreadex` and `WaitForSingleObject` do what the header says under the
// compiler atui is built with there, is unmeasured.  The OS/2 and POSIX halves were
// measured first precisely so that this file could be a translation of a known-working
// design rather than a guess at one.
#include "thread_platform.h"

#if defined( ATUI_HAVE_THREADS ) && defined( _WIN32 )

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <process.h>

namespace atui {
namespace core {

namespace {

unsigned __stdcall entry( void *self )
{
    Thread::runFor( self );
    // `_endthreadex` rather than a bare return: it is what lets the CRT release the
    // per-thread block it allocated for this thread.
    _endthreadex( 0 );
    return 0;
}

} // anonymous namespace

bool threadPlatformStart( Thread &self, void **handle, std::string *reason )
{
    unsigned tid = 0;
    const uintptr_t h = _beginthreadex( 0, 0, &entry, &self, 0, &tid );
    if( h == 0 )
    {
        if( reason )
            *reason = "cannot start a thread";
        return false;
    }

    handle[ 0 ] = reinterpret_cast< void * >( h );
    handle[ 1 ] = 0;
    return true;
}

void threadPlatformJoin( void **handle )
{
    if( handle[ 0 ] == 0 )
        return;
    const HANDLE h = reinterpret_cast< HANDLE >( handle[ 0 ] );

    WaitForSingleObject( h, INFINITE );
    CloseHandle( h );
    handle[ 0 ] = 0;
}

} // namespace atui::core
} // namespace atui

#endif // ATUI_HAVE_THREADS && _WIN32
