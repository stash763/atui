// The desktop seam where no desktop answers: every capability says no.
//
// This file exists for the same reason core/thread_none.cpp does -- an
// absent platform is a working answer, not a link error. Compiled whenever
// no real backend is: builds without X11, without the macOS helper, and
// (until the PM backend lands in the OS/2 tree) the OS/2 tree itself.
// See screen.h.
#include "platform/screen.h"

#if !defined( ATUI_HAVE_X11 ) && !defined( ATUI_HAVE_MAC_CAPTURE ) \
    && !defined( ATUI_HAVE_OS2_PM )

namespace atui {
namespace platform {

namespace {

const char *kNoBackend =
    "no screen access on this platform (no X11 display, and no capture "
    "helper in this build)";

} // anonymous namespace

bool captureAvailable() { return false; }
bool windowsAvailable() { return false; }

CaptureResult captureScreen( const std::string & )
{
    CaptureResult r;
    r.error = kNoBackend;
    return r;
}

CaptureResult captureWindow( const std::string &, const std::string & )
{
    CaptureResult r;
    r.error = kNoBackend;
    return r;
}

bool listWindows( std::vector<WindowInfo> &, bool )
{
    return false;
}

} // namespace atui::platform
} // namespace atui

#endif // no backend selected
