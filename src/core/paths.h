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

#include <cstdio>
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

// Asks the operating system to get a file's bytes down to the device: `fflush`,
// then the platform's flush-the-buffer call. It is the difference between "the
// bytes outlived atui" and "the bytes outlived the machine", and only the first
// one is free.
//
// Best effort, and it returns nothing because there is nothing useful to do with
// a failure: on a file system that will not say, a journal entry is still worth
// more than no journal entry, and refusing the edit over it would be refusing to
// work on machines that are merely slower to trust. `canFlushToDisk()` says whether
// asking is a real request or a no-op, which is what the documentation and the
// self-tests want to know.
void flushToDisk( std::FILE *file );

// Whether this platform has a call that means it. False is not a defect and not a
// challenge: MS-DOS and OS/2 are deliberately not asked, because there the call is
// either missing or "flush every buffer on the machine", which costs a soft-floppy
// user more than the bytes it protects. The promise atui can make there stops at
// "survived an atui crash".
bool canFlushToDisk();

// Absolute form of `path`, resolved the way the operating system resolves it -
// from the working directory, not from the project root. The two are normally the
// same directory, but a record of "which file did I change" has to name the one
// the write actually went to. Paths already absolute, and anything the platform
// considers absolute (a drive letter), come back unchanged.
std::string absolutePath( const std::string &path );

// Changes the working directory. Returns false when it cannot.
bool setWorkingDirectory( const std::string &path );

// --- path shapes -----------------------------------------------------------------
//
// Facts about how a path is *written*, answered for every shape atui can be handed
// on any platform it builds for - not only the one it was compiled for. A policy
// decision about `C:\Users\ann\.ssh\id_rsa` must not depend on which machine is
// asking the question, and the same self-tests then run identically everywhere.
//
// Both separators count as separators, so a POSIX file whose name contains a
// backslash is analysed as if it were nested. That can only turn an auto-approval
// into a question, never the other way round, which is the only direction worth
// being wrong in - and it also means `cat .\.ssh/config` is seen for the .ssh
// file that the shell would actually open.

// Whether names compare with case folded: the DOS-derived platforms fold case in
// file names and everything else does not. Compile-time by default, and settable
// so the self-tests check both behaviours on one host rather than leaving the
// Windows half of a change untested until someone cross-compiles it.
bool pathsAreCaseInsensitive();
void setPathsAreCaseInsensitive( bool value );

// The anchor of `path`: the part of it that names a device rather than a
// directory, in one canonical spelling regardless of which separators were typed.
// "" for a relative path, "/" for `/etc/hosts`, "C:/" for `C:\Windows`, "C:" for
// the drive-relative `C:notes.txt`, "//host/share" for `\\host\share\dir`. Empty
// means relative to the current directory, and only that.
std::string pathAnchor( const std::string &path );

// True for anything that names a file without reference to the current directory:
// `/etc/hosts`, `C:\Windows`, `C:/Windows`, `\\server\c$\share`, and the DOS
// drive-relative form `C:notes.txt` - absolute on drive C, but not somewhere this
// drive can put it.
bool isAbsolutePath( const std::string &path );

// True for `\\server\share\...`: the file is on another machine, so no directory
// on this one contains it, whatever the string looks like next to a root.
bool isUncPath( const std::string &path );

// True when `path` is a root rather than something below one: `/`, `C:`, `C:\`,
// `\\server\share`. A project rooted there has no edges to contain anything.
bool isPathRoot( const std::string &path );

// `child` is `parent` or below it, compared component by component rather than as
// text: /home/u/proj-extra is not inside /home/u/proj, `C:\PROJ\x` is inside
// `c:/proj` where case folds, and either separator may appear on either side.
// Both arguments have to be absolute and already collapsed (`..` removed); a
// relative argument or a different drive or share is never contained.
bool pathIsUnder( const std::string &child, const std::string &parent );

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_PATHS_H
