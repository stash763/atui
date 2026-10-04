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

} // namespace atui_test

#endif // ATUI_TESTS_HARNESS_H
