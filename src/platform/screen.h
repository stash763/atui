// The desktop seam: what this machine can show and what runs on it.
//
// Two capabilities, asked for and answered as strings and plain structs
// because both cross a boundary that way already: a model's tool call comes
// in as JSON and its result leaves as text. captureAvailable()/
// windowsAvailable() decide which tools get registered at all -- a model
// that cannot see the screen should never be told it can -- and the
// backends (screen_x11.cpp, screen_mac.cpp, screen_os2.cpp, and
// screen_none.cpp as the answer for platforms with no graphical session)
// are each compiled away to nothing where they do not apply.
//
// Window identities are opaque strings: X11 answers with hex window ids,
// OS/2 will answer with hex HWNDs, and nothing above this header should
// care, because the only contract is round-trip -- list them, hand one
// back, get that window.
//
// C++98, standard library only.
#ifndef ATUI_PLATFORM_SCREEN_H
#define ATUI_PLATFORM_SCREEN_H

#include <string>
#include <vector>

namespace atui {
namespace platform {

struct WindowInfo {
    std::string handle;    // opaque id ("0x1a00003" on X11)
    std::string title;     // as the window names itself, raw encoding
    std::string cls;       // resource class (WM_CLASS), empty when none
    int x, y, w, h;        // screen pixels, top-left origin
    bool active;           // what the desktop considers focused
    bool iconic;           // minimized: it exists, nothing of it shows
    std::vector<WindowInfo> children;  // one level, when asked for

    WindowInfo() : x( 0 ), y( 0 ), w( 0 ), h( 0 ),
                   active( false ), iconic( false ) {}
};

struct CaptureResult {
    bool ok;
    std::string error;     // why not, when ok == false
    std::string path;      // the file written, when ok == true
    int width, height;     // pixels, when ok == true

    CaptureResult() : ok( false ), width( 0 ), height( 0 ) {}
};

// Whether pixels can be grabbed here at all: an X connection can be opened,
// the platform helper exists. A yes here is an invitation to try, not a
// promise -- the capture functions still answer with errors.
bool captureAvailable();

// Whether windows can be listed. Same rules as captureAvailable().
bool windowsAvailable();

// Grabs to `path` (".bmp" appended when the name has no extension).
// captureWindow takes the handle string from a WindowInfo and asks for
// the window's own pixels: a window's screenshot is its contents, not
// the fragment of it that happens to survive behind others -- which is
// also the method that survives compositors, measured against XQuartz.
CaptureResult captureScreen( const std::string &path );
CaptureResult captureWindow( const std::string &handle,
                             const std::string &path );

// Top-level windows, in stacking-unrelated order backends find natural.
// `children` fills the one child level: asking the X server once is much
// cheaper than asking twice, so the choice belongs to the caller.
bool listWindows( std::vector<WindowInfo> &out, bool children );

// --- Pure parts, shared by the tools and testable without a desktop. ---

// The windows listing as the model sees it: one line per window, children
// indented. Titles are folded for the transcript here, where they become
// text a seven-bit screen will show.
std::string formatWindows( const std::vector<WindowInfo> &list );

// Finds windows by query: an exact handle first, else a case-insensitive
// substring of the title, else of the class. Empty query matches nothing:
// the callers list explicitly, they do not guess. Top-level windows only
// -- children ride along in the listing but are not searchable, which
// keeps "what did the user point at" a question with one answerable
// meaning.
void findWindows( const std::vector<WindowInfo> &list,
                  const std::string &query,
                  std::vector< const WindowInfo * > &out );

// "atui_shot_1.bmp" in the working directory, bumping past existing files.
std::string defaultShotPath();

// A requested path with an image extension appended when it has none, so a
// model that said "path":"result" gets result.bmp and not a header-less
// mystery.
std::string ensureImagePath( const std::string &path );

} // namespace atui::platform
} // namespace atui

#endif // ATUI_PLATFORM_SCREEN_H
