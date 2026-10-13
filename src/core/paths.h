// Where atui came from, and the handful of filesystem facts that are not
// actually portable.
//
// atui runs from Linux to MS-DOS, so the portable way to find "the directory
// this program was started from" is the one mechanism every platform provides
// and none of them disagree about: argv[0]. Nothing here reads /proc/self/exe or
// calls GetModuleFileName, because both are missing somewhere on that list.
//
// C++14, no TUI and no network dependency.
#ifndef ATUI_CORE_PATHS_H
#define ATUI_CORE_PATHS_H

#include <string>

namespace atui {
namespace core {

// Captured from main() before anything else can move the working directory.
// Passing 0 is allowed and leaves the previous value alone.
void setArgv0( const char *argv0 );

// The directory atui is installed in, which is also where its own files live -
// the undo journal, and anything else that belongs to the program rather than to
// the user or the project. Resolution order:
//
//   ATUI_HOME           the escape hatch: a read-only or packaged install
//   dirname(argv[0])    the normal case, and portable by construction
//   ATUI_INSTALL_DIR    baked in by CMake, for launchers that drop argv[0]
//   "."                 last resort: writable in practice, and in the user's face
//
// Never empty.
std::string installDir();

// --- filesystem facts ----------------------------------------------------------

bool isDirectory( const std::string &path );

// Creates one directory level. The 16-bit toolchains take no mode argument, which
// is the only reason this exists instead of a mkdir call.
bool makeDirectory( const std::string &path );

// Whether atui could write into `path` (an existing directory).
bool isWritableDir( const std::string &path );

// Whether the file itself can be written. On Windows this is the read-only
// attribute, which is why the question is worth asking before opening anything:
// a failed open-after-truncate is how a tool turns one refused edit into a lost
// file. FAT has no write permission at all, so there it reports true and the
// create fails later.
bool isWritableFile( const std::string &path );

// `dir` + `leaf` with exactly one separator between them.
std::string joinDir( const std::string &dir, const std::string &leaf );

// Size of an existing file, or -1.
long fileSize( const std::string &path );

// Absolute form of `path`, resolved the way the operating system resolves it -
// from the working directory, not from the project root. The two are normally the
// same directory, but a record of "which file did I change" has to name the one
// the write actually went to. Paths already absolute, and anything the platform
// considers absolute (a drive letter), come back unchanged.
std::string absolutePath( const std::string &path );

// Changes the working directory. Returns false when it cannot.
bool setWorkingDirectory( const std::string &path );

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_PATHS_H
