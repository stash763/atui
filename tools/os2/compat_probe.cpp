/* compat_probe.cpp -- run the headers in compat/os2/include against the kernel.

   Development probe, not part of the program: `build.cmd link compat_probe.cpp
   compat_probe.exe`, run it, read what it prints.  Each header in that directory
   is a re-implementation of something atui normally gets from a C++ library, so
   each one is asked here whether it behaves like the thing it replaces -- and the
   two OS/2 behaviours the re-implementations depend on (a timed wait returning
   ERROR_TIMEOUT rather than hanging, and a directory search reporting a read-only
   file, which decides whether undo-journal recovery can see its own .PRE files)
   are measured rather than assumed.

   The numbers the output should show are written into each line: if a line
   disagrees with its own expectation it is a bug in this tree or in the kernel,
   and either way it is worth stopping for.
*/
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <dirent.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>            /* mkdir, rmdir: not in <sys/stat.h> here */
#include <sys/stat.h>

#include <map>
#include <string>
#include <vector>

static std::mutex     g_staticMutex;   /* constructed before main(): is that legal? */
static int            g_counter = 0;

/* A predicate in C++98 is a function object.  tools/approval.cpp waits on
   exactly this shape. */
struct IsSet {
    bool *flag_;
    explicit IsSet( bool *f ) : flag_( f ) {}
    bool operator()() const { return *flag_; }
};

static void report( const char *what, bool ok )
{
    printf( "  %s %s\n", ok ? "ok  " : "FAIL", what );
}

int main()
{
    setvbuf( stdout, NULL, _IONBF, 0 );

    printf( "== mutex, global and local ==\n" );
    {
        std::lock_guard< std::mutex > lock( g_staticMutex );
        ++g_counter;
        report( "a file-scope std::mutex locks in main()", g_counter == 1 );
        printf( "  locked twice from one thread: try_lock says %d (a real std::mutex says 0;\n"
                "  the kernel's is recursive, so this one says 1 -- documented in mutex)",
                (int) g_staticMutex.try_lock() );
        g_staticMutex.unlock();
    }
    {
        std::mutex  m;
        std::unique_lock< std::mutex > lock( m );
        printf( "  unique_lock owns_lock: held=%d ", (int) lock.owns_lock() );
        lock.unlock();
        printf( "after unlock=%d\n", (int) lock.owns_lock() );
        report( "try_lock on a free mutex takes it", m.try_lock() );
        m.unlock();
    }

    printf( "== condition_variable ==\n" );
    {
        std::mutex               m;
        std::condition_variable  cv;
        bool                     ready = false;
        std::unique_lock< std::mutex > lock( m );

        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        bool answered = cv.wait_for( lock, std::chrono::milliseconds( 250 ), IsSet( &ready ) );
        long waited = (long) std::chrono::duration_cast< std::chrono::milliseconds >(
            std::chrono::steady_clock::now() - start ).count();
        printf( "  nobody posted: result=%d after %ld ms (want 0 after ~250)\n",
                (int) answered, waited );
        report( "a timeout is a timeout, not an answer", !answered && waited >= 200 && waited < 2000 );

        ready = true;
        report( "predicate already true returns true at once",
                cv.wait_for( lock, std::chrono::milliseconds( 5000 ), IsSet( &ready ) ) );

        /* The lost-wakeup case: the state changes and is posted before the waiter
           arrives.  The predicate has to be what saves it, not the post. */
        ready = false;
        cv.notify_all();
        report( "a post with no waiter does not wedge the next wait",
                !cv.wait_for( lock, std::chrono::milliseconds( 200 ), IsSet( &ready ) ) );
        ready = true;
        report( "and the next real answer still arrives",
                cv.wait_for( lock, std::chrono::milliseconds( 200 ), IsSet( &ready ) ) );

        report( "wait( pred ) returns as soon as the predicate is true",
                ( cv.wait( lock, IsSet( &ready ) ), true ) );
        lock.unlock();
    }

    printf( "== chrono ==\n" );
    {
        std::chrono::steady_clock::time_point a = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point b = a + std::chrono::seconds( 3 );
        long threeSeconds = (long) ( b - a ).count();
        printf( "  now + 3 seconds - now = %ld ms (want 3000)\n", threeSeconds );
        report( "seconds are not milliseconds", threeSeconds == 3000 );

        long quarter = (long) ( a + std::chrono::milliseconds( 250 ) - a ).count();
        printf( "  now + 250 milliseconds - now = %ld ms (want 250)\n", quarter );
        report( "milliseconds are milliseconds", quarter == 250 );
        report( "time_point compares", a < b && b > a );

        std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();
        DosSleep( 300 );
        std::chrono::steady_clock::time_point after = std::chrono::steady_clock::now();
        long slept = (long) std::chrono::duration_cast< std::chrono::milliseconds >(
            after - before ).count();
        printf( "  DosSleep( 300 ) measured as %ld ms\n", slept );
        report( "the clock runs at the right speed", slept >= 290 && slept < 1500 );

        long count = (long) std::chrono::duration_cast< std::chrono::milliseconds >(
            std::chrono::seconds( 90 ) ).count();
        printf( "  duration_cast< milliseconds >( 90 seconds ) = %ld (want 90000)\n", count );
        report( "duration_cast converts", count == 90000 );
    }

    printf( "== dirent ==\n" );
    {
        const char *dir  = "d:\\atui\\cptest";
        const char *plain = "d:\\atui\\cptest\\plain.txt";
        const char *ro    = "d:\\atui\\cptest\\entry.pre";
        const char *hid   = "d:\\atui\\cptest\\hidden.inf";

        mkdir( dir );    /* existing is fine: this is a scratch directory */

        FILE *f = fopen( plain, "w" );
        if( f ) { fputs( "1", f ); fclose( f ); }
        f = fopen( ro, "w" );
        if( f ) { fputs( "2", f ); fclose( f ); }
        f = fopen( hid, "w" );
        if( f ) { fputs( "3", f ); fclose( f ); }
        int chmodRc = _chmod( ro, 0 );          /* read-only, like a journal .PRE */
        printf( "  made three files, _chmod( ro, 0 ) rc=%d\n", chmodRc );

        int seen = 0, plainSeen = 0, roSeen = 0, hidSeen = 0;
        DIR *d = opendir( dir );
        printf( "  opendir -> %s\n", d ? "a handle" : "NULL" );
        if( d )
        {
            struct dirent *e = 0;
            while( ( e = readdir( d ) ) != 0 )
            {
                printf( "    entry: %s\n", e->d_name );
                ++seen;
                if( strcmp( e->d_name, "plain.txt" ) == 0 )  ++plainSeen;
                if( strcmp( e->d_name, "entry.pre" ) == 0 )  ++roSeen;
                if( strcmp( e->d_name, "hidden.inf" ) == 0 ) ++hidSeen;
            }
            closedir( d );
        }
        printf( "  %d entries: plain=%d read-only=%d hidden-or-not=%d\n",
                seen, plainSeen, roSeen, hidSeen );
        report( "the read-only entry is listed (the journal depends on it)", roSeen == 1 );
        report( "the ordinary entries are listed", plainSeen == 1 && hidSeen == 1 );

        /* readdir twice in a row must not repeat or skip. */
        d = opendir( dir );
        if( d )
        {
            std::vector< std::string > once, twice;
            struct dirent *e = 0;
            while( ( e = readdir( d ) ) != 0 ) once.push_back( e->d_name );
            closedir( d );
            d = opendir( dir );
            while( d && ( e = readdir( d ) ) != 0 ) twice.push_back( e->d_name );
            if( d ) closedir( d );
            report( "two listings of the same directory agree", once == twice );
        }

        _chmod( ro, 0600 );
        remove( plain );
        remove( ro );
        remove( hid );
        rmdir( dir );

        DIR *missing = opendir( "d:\\atui\\no-such-directory-here" );
        report( "opendir of a missing directory returns NULL, not a handle", missing == 0 );
        if( missing ) closedir( missing );
    }

    printf( "== the library these headers sit next to ==\n" );
    {
        std::string                       s = "abc";
        std::vector< std::string >        v;
        std::map< std::string, int >      m;
        v.push_back( s );
        m[ s ] = 7;
        s += 'd';
        s = s.substr( 1, 2 );
        printf( "  string=\"%s\" size=%u substr=%s map=%d vector=%u\n",
                s.c_str(), (unsigned) s.size(), s.substr( 0, 1 ).c_str(), m[ "abc" ],
                (unsigned) v.size() );
        report( "std::string, vector and map work together",
                s.size() == 2 && m[ "abc" ] == 7 && v.size() == 1 );
    }

    printf( "COMPAT-PROBE-DONE\n" );
    return 0;
}
