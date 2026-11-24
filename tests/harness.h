// Minimal self-test harness for the portable core.
//
// C++98. register_test() is called once from atui_test::init(); run()
// executes every registered test and reports to stderr.

#ifndef ATUI_TESTS_HARNESS_H
#define ATUI_TESTS_HARNESS_H

#include <string>

namespace atui_test {

// Register a test (implemented by tests/selftest.cpp).
void init();
// Register a named test; fn returns 0 on success, non-zero on failure.
void register_test( const char *name, int (*fn)( void ) );
// Execute all registered tests. Returns 0 if all passed.
int run();

// Put a variable in the environment, or take it out.  The suites need this to
// point atui at a config file or hand it a credential, and there is no single
// spelling for it: POSIX has setenv() and unsetenv(), Windows has neither and
// does both jobs with _putenv_s().  It is named once here for the same reason
// makeDir() exists in selftest.cpp.  Every suite that grew its own platform
// branch is a suite that stops being tested the moment the branch is wrong,
// which is what happened first up on Windows.
void set_env( const char *name, const char *value );
void unset_env( const char *name );

// A directory the suites may write into, without a trailing separator.
// There is no /tmp on Windows, which says TMP or TEMP instead -- so a suite that
// hardwires /tmp does not fail loudly, it fails as whatever it was testing,
// because the fixture it wrote never appeared.  This rule was written three
// times in two files and only two of them looked at TEMP; the third was
// toolTmpPath(), and its tests were the first to go red on Windows.
std::string temp_dir();

// The process's working directory, for the test that has to move it and put it
// back.  POSIX spells it getcwd() and Windows _getcwd(); named once for the same
// reason set_env() is.  A test that changes the working directory without restoring
// it changes every test that runs after it, which is a failure that shows up in
// somebody else's suite.
std::string current_dir();

// Makes and removes one directory.  Named once for the reason set_env() is: MSVC puts
// them in <direct.h> as `_mkdir` and `_rmdir`, and `_mkdir` takes no mode, so a suite
// that writes the POSIX call at every site learns about Windows by failing to build --
// and a suite that does not build is a suite that is not tested.  Returns what the
// platform call returns, because a fixture that tolerates "already exists" and one that
// requires it are two different assertions worth making.
int makeDir( const std::string &path );
int removeDir( const std::string &path );

// Makes a file read-only, or lets it be written again, and says whether the host has any
// such idea.  This is the seam that the read-only refusal test was missing: it spelled
// the call `chmod`, so it went inside a `#if !defined(_WIN32)` and a promise about every
// platform -- refuse before copying or truncating -- stopped being tested on one of them.
// On Windows the idea is a file attribute -- the one `attrib +R` sets -- and
// `isWritableFile()`'s `_access(path, 2)` reports the result as -1/EACCES (measured), so
// the refusal is a real behaviour there and worth watching fail.  False means no
// assertion was made, which a caller reports as a skip rather than a pass.
bool set_read_only( const std::string &path, bool readOnly );

// A test returns 0 when it passes and anything else when it fails.  Some assertions
// need a capability the machine may not have -- creating a symbolic link is the one
// that forced this to exist, needing a privilege or developer mode -- and neither
// existing answer is true there: failing blames the software for the host, and
// passing claims a check that was never made.  Return skip_test( why ) instead.
// The reason is printed and counted in the summary, so a suite that checks less than
// it used to says so in its own line rather than looking like a suite that passes.
int skip_test( const char *why );

// Creates a link at `link` pointing at `target`, for the tests that judge a link by
// where it leads.  Returns false when the host will not do it, and a caller that
// ignores that has to report a skip: an assertion about a link that was never
// created is not an assertion.  `directory` matters on Windows, where a symbolic
// link records which kind it is when it is made.  Both symbolic links and junctions
// are reparse points, and atui's policy is about the class, so either serves the
// test -- but only a symbolic link can point at a file, which is what the deny-list
// case needs.
bool make_link( const std::string &target, const std::string &link, bool directory );

// A command that takes roughly `seconds` to finish, in the syntax of the shell
// atui runs commands through.  POSIX has sleep(); a base Windows install has no
// sleep and no other way to wait for a moment, and `ping -n N` at one reply per
// second is the spelling everyone uses (measured on the target: -n 4 took 3.1s).
// Named once because a deadline tested on one platform only is how Windows ended
// up with a timeout that was never enforced and never mentioned.
std::string wait_command( long seconds );

// A command that reads standard input until end-of-file and prints nothing: `cat`
// on POSIX, `more` on Windows, measured to exit 0 with no output when stdin is the
// NUL device atui gives every command.
std::string stdin_command();

// A command that creates an empty file at `path`, and a command that runs another one
// from inside a directory.  Both shells differ in the verb and in what a quote means:
// POSIX has `touch`, and single quotes that are quoting there and ordinary characters
// in cmd, where the second case does not quietly do the wrong thing but says "the
// filename, directory name, or volume label syntax is incorrect" (measured, as was that
// `type nul > "path"` creates the file with either separator, which matters because a
// fixture path is built as temp_dir() + "/name").  Named once for the reason
// wait_command() is: a command spelled for one shell, written into a test, does not
// fail there -- it passes without testing anything.
std::string make_file_command( const std::string &path );
std::string in_dir_command( const std::string &dir, const std::string &command );

// A command that starts a second one running in the background and then waits --
// the case a timeout has to reach through, since what hangs is rarely the command
// itself and usually something it started.
std::string background_wait_command( long seconds );

// Wraps a string as a JSON string literal's contents -- backslash and quote
// escaped -- for the tests that build a tool call by concatenating one.  A
// Windows path is the case that makes it necessary: "C:\Users\stash" is not a
// JSON string, and json_args.cpp's reader is lenient about unknown escapes in
// the way that loses information (it drops the backslash), so the path silently
// becomes C:Usersstash rather than failing.
//
// atui::net::jsonEscape() already does this, and is not used here because it
// lives in json_extract.cpp, which is only compiled when ATUI_HAVE_NETWORK is
// on -- the first link of a network-off build found that out.  Twelve lines in
// the harness beats making the suite depend on the network build to test paths.
std::string json_str( const std::string &s );

} // namespace atui_test

#endif // ATUI_TESTS_HARNESS_H
