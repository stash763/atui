#include "harness.h"
#include <cstdio>
#include <exception>

namespace atui_test {

namespace {
    // A fixed array rather than a container, because this file is compiled into
    // the C++98 build as well and has to work in a Watcom program with no
    // allocator to spare.  The number is generous and going over it is an error
    // the run reports, not something it quietly absorbs: with a silent cap the
    // summary says "all passed" about tests that never ran, which is the one thing
    // a test harness must not do.  (It happened at 128 -- ten tests were added, the
    // total stayed at 128, and four markdown tests stopped existing.)
    enum { kMaxTests = 512 };

    struct Entry {
        const char *name;
        int (*fn)( void );
    };

    Entry m_entries[ kMaxTests ];
    int m_count = 0;
    int m_dropped = 0;
}

void register_test( const char *name, int (*fn)( void ) )
{
    if( m_count < kMaxTests )
        m_entries[ m_count++ ] = { name, fn };
    else
        ++m_dropped;      // reported by run(), which then fails
}

int run()
{
    int passed = 0;
    int failed = 0;

    if( m_dropped > 0 )
    {
        std::fprintf( stderr,
                      "self-test: %d test(s) were NEVER REGISTERED: the harness "
                      "holds %d. Raise kMaxTests in tests/runner.cpp -- a run that "
                      "silently omits tests reports success about work it did not "
                      "do.\n", m_dropped, kMaxTests );
        ++failed;
    }

    for( int i = 0; i < m_count; ++i ) {
        const Entry &e = m_entries[ i ];
        int r = 0;
        try {
            r = e.fn();
        } catch( const std::exception &s ) {
            std::fprintf( stderr, "self-test %s threw: %s\n", e.name, s.what() );
            r = -1;
        } catch( ... ) {
            std::fprintf( stderr, "self-test %s threw (unknown type)\n", e.name );
            r = -1;
        }

        if( r == 0 ) {
            ++passed;
            std::fprintf( stderr, "self-test PASS %s\n", e.name );
        } else {
            ++failed;
            std::fprintf( stderr, "self-test FAIL %s (rc=%d)\n", e.name, r );
        }
    }

    std::fprintf( stderr, "self-test summary: %d passed, %d failed\n", passed, failed );
    return ( failed == 0 ) ? 0 : 1;
}

} // namespace atui_test
