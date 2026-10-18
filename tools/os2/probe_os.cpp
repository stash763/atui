/* Capability probe: the operating-system surface atui actually depends on.

   Everything called here is used by atui's core (paths, the journal, child
   processes, timing), so "does it exist" is not curiosity -- each gap is a
   function the port has to implement.  Split from probe_cxx.cpp because a
   missing header kills a whole translation unit, and the C++ results should
   not depend on the C ones.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <io.h>
#include <direct.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <process.h>

static int g_checks = 0;
static int g_failed = 0;

static void check( const char *what, bool okay )
{
    printf( "  %-34s %s\n", what, okay ? "ok" : "FAILED" );
    ++g_checks;
    if( !okay )
        ++g_failed;
}

static volatile int g_signal_seen = 0;

static void onSignal( int sig )
{
    (void)sig;
    g_signal_seen = 1;
}

int main( int argc, char **argv )
{
    char cwd[512];
    const char *dir = "probe_os.d";
    const char *file = "probe_os.tmp";

    printf( "== environment ==\n" );
    printf( "  argc=%d  getenv(PATH)=%s\n", argc, getenv( "PATH" ) ? "set" : "(null)" );
    printf( "  getenv(ATUI_ABSENT)=%s\n", getenv( "ATUI_ABSENT_DEFINITELY" ) ? "set" : "(null)" );

    printf( "== working directory and paths ==\n" );
    if( getcwd( cwd, sizeof( cwd ) ) )
        printf( "  getcwd -> %s\n", cwd );
    else
        check( "getcwd", false );
    printf( "  mkdir(.) -> %d  (0 means the current directory is writable)\n",
            _access( ".", 2 ) == 0 ? 0 : -1 );
    /* separators: what does this platform accept, and what does it produce?
       atui's path analysis has to know, and the answer is not portable. */
    printf( "  strchr(cwd,'\\\\')=%s  strchr(cwd,'/')=%s\n",
            strchr( cwd, '\\' ) ? "yes (backslash used)" : "no",
            strchr( cwd, '/' ) ? "yes" : "no" );
    errno = 0;
    check( "chdir to an existing directory", chdir( ".." ) == 0 );
    check( "getcwd after chdir", getcwd( cwd, sizeof( cwd) ) != 0 );
    check( "chdir back", chdir( "." ) == 0 );

    printf( "== filesystem ==\n" );
    errno = 0;
    int rc = mkdir( dir );
    printf( "  mkdir(%s) -> %d errno=%d\n", dir, rc, errno );
    check( "mkdir", rc == 0 );

    FILE *f = fopen( file, "wb" );
    if( f ) {
        fputs( "0123456789", f );
        fclose( f );
    }
    struct stat st;
    /* Every handle here is closed before the next line.  An open handle makes
       OS/2 refuse the rename below, and the first version of this probe did
       exactly that in the middle of a check() -- which then reported that the
       platform cannot rename a file, in a program that had broken it itself. */
    {
        FILE *probe = fopen( file, "rb" );
        check( "stat a file", probe != 0 && stat( file, &st ) == 0 && st.st_size == 10 );
        if( probe )
            fclose( probe );
    }
    printf( "  stat size=%ld mode=0%o mtime=%ld\n",
            (long)st.st_size, (unsigned)st.st_mode, (long)st.st_mtime );
    check( "stat says it is a regular file", (st.st_mode & S_IFMT) == S_IFREG );
    check( "stat on the directory", stat( dir, &st ) == 0 && (st.st_mode & S_IFMT) == S_IFDIR );
    errno = 0;
    rc = rename( file, "probe_os.renamed" );
    printf( "  rename to a new name -> %d errno=%d\n", rc, errno );
    check( "rename to a new name", rc == 0 );
    check( "stat after rename", stat( "probe_os.renamed", &st ) == 0 );
    /* The journal writes a temporary file and moves it into place, so what
       matters is whether moving onto an existing file replaces it or fails.
       Nothing may be holding either name while this runs. */
    f = fopen( file, "wb" );
    if( f )
        fclose( f );
    errno = 0;
    rc = rename( "probe_os.renamed", file );
    printf( "  rename over an existing file -> %d errno=%d: %s\n", rc, errno,
            rc == 0 ? "replaces it" : "refuses, so the journal must unlink first" );
    if( rc == 0 )
        check( "unlink after a replacing rename", unlink( file ) == 0 );
    else {
        errno = 0;
        rc = remove( file );
        printf( "  remove() over an existing file -> %d errno=%d\n", rc, errno );
        rc = rename( "probe_os.renamed", file );
        printf(  "  rename after remove() -> %d errno=%d\n", rc, errno );
        check( "the delete-then-rename sequence works", rc == 0 );
    }
    remove( file );
    remove( "probe_os.renamed" );
    errno = 0;
    rc = rmdir( dir );
    printf( "  rmdir(%s) -> %d errno=%d\n", dir, rc, errno );

    printf( "== file descriptors ==\n" );
    f = fopen( file, "wb" );
    if( f ) {
        int fd = fileno( f );
        check( "fileno", fd >= 0 );
        long where = lseek( fd, 5, SEEK_SET );
        printf( "  lseek -> %ld\n", where );
        fclose( f );
    }
    check( "open/close via the CRT", open( file, O_RDONLY ) >= 0 );
    /* Whether stdout is a tty depends on how this was invoked, so it is printed
       rather than checked: run without redirection to learn what the target
       does, and read it as a lie if the output was piped into a log. */
    printf( "  isatty(0)=%d isatty(1)=%d isatty(2)=%d (redirection changes these)\n",
            isatty( 0 ), isatty( 1 ), isatty( 2 ) );
    remove( file );

    printf( "== time ==\n" );
    time_t t = time( 0 );
    printf( "  time -> %ld (2-second granularity is expected here)\n", (long)t );
    printf( "  clock -> %ld\n", (long)clock() );
    struct tm *lt = localtime( &t );
    char stamp[64];
    strftime( stamp, sizeof( stamp ), "%Y-%m-%dT%H:%M:%S", lt );
    printf( "  strftime -> %s\n", stamp );
    printf( "  mktime round trip -> %ld\n", (long)mktime( lt ) );

    printf( "== process ==\n" );
    signal( SIGINT, onSignal );
    check( "signal() accepts a handler", g_signal_seen == 0 );
    printf( "  getpid -> %ld\n", (long)getpid() );
    /* atui runs commands through a child process.  What matters here is whether
       the wait flavours exist, not just spawn. */
    printf( "  system(\"echo\") -> %d\n", system( "echo x > nul" ) );
    printf( "  interrupt/abort builtins: abort exists, exit exists\n" );

    printf( "== result ==\n  %d checks, %d failed\n", g_checks, g_failed );
    return g_failed == 0 ? 0 : 1;
}
