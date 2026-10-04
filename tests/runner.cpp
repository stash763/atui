#include "harness.h"
#include <cstdio>
#include <exception>

namespace atui_test {

namespace {
    enum { kMaxTests = 128 };

    struct Entry {
        const char *name;
        int (*fn)( void );
    };

    Entry m_entries[ kMaxTests ];
    int m_count = 0;
}

void register_test( const char *name, int (*fn)( void ) )
{
    if( m_count < kMaxTests )
        m_entries[ m_count++ ] = { name, fn };
}

int run()
{
    int passed = 0;
    int failed = 0;

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
