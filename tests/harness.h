// Minimal self-test harness for the portable core.
//
// C++98. register_test() is called once from atui_test::init(); run()
// executes every registered test and reports to stderr.

#ifndef ATUI_TESTS_HARNESS_H
#define ATUI_TESTS_HARNESS_H

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

} // namespace atui_test

#endif // ATUI_TESTS_HARNESS_H
