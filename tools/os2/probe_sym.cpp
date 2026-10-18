/* Symbol probe: which of the POSIX-shaped functions atui calls exist here?

   Deliberately no headers for the questionable ones.  A missing *header* fails
   a translation unit one function at a time, which makes the survey take one
   round trip per gap; declaring the prototypes by hand instead means the
   *linker* reports every missing symbol in a single message.  The undefined
   list is the result this file exists to produce -- if it links, they all
   exist; if it does not, the errors are the port's to-do list.

   The signatures only have to be plausible, not correct: nothing here runs.

   Build and read the errors:

     wpp386 -bt=os2 -fo=probe_sym.obj probe_sym.cpp
     wlink system os2v2 name probe_sym.exe file probe_sym.obj
*/

#include <stdio.h>

/* extern "C" is load-bearing, not ceremony.  This file is C++, so without it
   every hand-declared prototype below is mangled as a C++ name, every one of
   them comes back undefined, and the probe confidently reports that the target
   has no strdup and no fsync -- which happened, and is how this comment got
   written.  With C linkage the linker's undefined list means what it says. */
extern "C" {

/* --- directory traversal (atui walks trees for the journal and file list) */
struct my_dirent
{
    char d_name[300];
};
extern void *opendir( const char * );
extern struct my_dirent *readdir( void * );
extern int closedir( void * );
extern int scandir( const char *, void ***, void *, int );
extern int alphasort( void *, void * );

/* --- multiplexing (the network loop waits on a socket) */
struct my_pollfd
{
    int fd;
    short events;
    short revents;
};
extern int poll( struct my_pollfd *, unsigned, int );
extern int select( int, void *, void *, void *, void * );

/* --- child processes (run_command_tool waits for exit status) */
extern int waitpid( int, int *, int );
extern int wait( int * );
extern int wait4( int, int *, int, void * );
extern int kill( int, int );
extern int execle( const char *, ... );
extern int execlp( const char *, ... );
extern int posix_spawn( int *, const char *, void *, void *, char *const [], char *const [] );
extern void *_beginthread( void (*)( void * ), unsigned, void * );
extern int _beginthreadex( void *, unsigned, unsigned (*)( void * ), void *, unsigned, unsigned * );
extern void _endthreadex( unsigned );

/* --- threads and locks (40 uses in atui; the C++ <mutex> is absent) */
extern int pthread_create( void *, const void *, void *(*)( void * ), void * );
extern int pthread_join( void *, void ** );
extern int pthread_mutex_init( void *, const void * );
extern int pthread_mutex_lock( void * );
extern int pthread_cond_wait( void *, void * );
extern int pthread_cond_signal( void * );

/* --- time */
struct my_timeval
{
    long tv_sec;
    long tv_usec;
};
extern int gettimeofday( struct my_timeval *, void * );
extern unsigned int usleep( unsigned int );
extern int nanosleep( void *, void * );
extern unsigned int sleep( unsigned int );

/* --- durability (the journal promises bytes are on disk) */
extern int fsync( int );
extern int fdatasync( int );
extern int msync( void *, unsigned long, int );
extern int sync( void );

/* --- filesystem details */
extern int symlink( const char *, const char * );
extern int link( const char *, const char * );
extern int readlink( const char *, char *, unsigned long );
extern char *realpath( const char *, char * );
extern char *mkdtemp( char * );
extern int chmod( const char *, int );
extern int utime( const char *, void * );
extern int statvfs( const char *, void * );
extern long long disk_free_clusters( int );

/* --- misc */
extern int setenv( const char *, const char *, int );
extern int unsetenv( const char * );
extern char **environ;
extern int sigaction( int, void *, void * );
extern int sigprocmask( int, void *, void * );
extern FILE *popen( const char *, const char * );
extern int pclose( FILE * );
extern char *strdup( const char * );
extern int strcasecmp( const char *, const char * );
extern unsigned long long strtoull( const char *, char **, int );
extern int asprintf( char **, const char *, ... );
extern char *strtok_r( char *, const char *, char ** );

/* Referencing a function's address is enough to need it at link time; folding
   every result into a volatile keeps the compiler from discarding the calls
   that it could otherwise prove useless. */
/* --- Watcom's own spellings, because the POSIX name is not always the one
   this library uses and the answer decides whether the port wraps a function
   or writes it from scratch */
struct my_findinfo
{
    unsigned attrs;
    unsigned time, date;
    long size;
    char name[15];
};
extern void *_findfirst( const char *, struct my_findinfo *, int );
extern int _findnext( void *, struct my_findinfo * );
extern int _findclose( void * );
extern int _chmod( const char *, int );
extern char *_getcwd( char *, int );
extern int _mkdir( const char * );
extern int _stat( const char *, void * );
extern int _snprintf( char *, size_t, const char *, ... );
extern int _vsnprintf( char *, size_t, const char *, void * );
extern long _timezone;

/* --- OS/2 native, what the thread and process shims would call */
extern int DosCreateThread( void *, unsigned *, unsigned (*)( void * ), unsigned, unsigned, void * );
extern int DosWaitChild( void *, unsigned, void *, void * );
extern int DosCreateMutexSem( char *, void *, unsigned, unsigned );
extern int DosRequestMutexSem( void *, unsigned );
extern int DosCreateEventSem( char *, void *, unsigned, unsigned );
extern int DosSleep( unsigned );
extern int DosQuerySysInfo( int, int, void *, unsigned );

} /* extern "C" */

static volatile long g_sink;

int main( void )
{
    struct my_pollfd pfd;
    struct my_timeval tv;
    char scratch[4];
    scratch[0] = 0;

    g_sink += (long)opendir( "." );
    g_sink += (long)readdir( 0 );
    g_sink += closedir( 0 );
    g_sink += scandir( ".", 0, 0, 0 );
    g_sink += (long)alphasort( 0, 0 );

    g_sink += poll( &pfd, 0, 0 );
    g_sink += select( 0, 0, 0, 0, 0 );

    g_sink += waitpid( 0, 0, 0 );
    g_sink += wait( 0 );
    g_sink += wait4( 0, 0, 0, 0 );
    g_sink += kill( 0, 0 );
    g_sink += execle( "x", "x", 0 );
    g_sink += execlp( "x", "x", 0 );
    g_sink += posix_spawn( 0, "x", 0, 0, 0, 0 );
    g_sink += (long)_beginthread( 0, 0, 0 );
    g_sink += _beginthreadex( 0, 0, 0, 0, 0, 0 );
    _endthreadex( 0 );

    g_sink += pthread_create( 0, 0, 0, 0 );
    g_sink += pthread_join( 0, 0 );
    g_sink += pthread_mutex_init( 0, 0 );
    g_sink += pthread_mutex_lock( 0 );
    g_sink += pthread_cond_wait( 0, 0 );
    g_sink += pthread_cond_signal( 0 );

    g_sink += gettimeofday( &tv, 0 );
    g_sink += usleep( 0 );
    g_sink += nanosleep( 0, 0 );
    g_sink += sleep( 0 );

    g_sink += fsync( 0 );
    g_sink += fdatasync( 0 );
    g_sink += msync( 0, 0, 0 );
    sync();

    g_sink += symlink( "a", "b" );
    g_sink += link( "a", "b" );
    g_sink += readlink( "a", scratch, 0 );
    g_sink += (long)realpath( "a", 0 );
    g_sink += (long)mkdtemp( scratch );
    g_sink += chmod( "a", 0 );
    g_sink += utime( "a", 0 );
    g_sink += statvfs( ".", 0 );

    g_sink += setenv( "A", "B", 0 );
    g_sink += unsetenv( "A" );
    g_sink += (long)environ;
    g_sink += sigaction( 0, 0, 0 );
    g_sink += sigprocmask( 0, 0, 0 );
    g_sink += (long)popen( "x", "r" );
    g_sink += pclose( 0 );
    g_sink += (long)strdup( "x" );
    g_sink += strcasecmp( "a", "B" );
    g_sink += (long)strtoull( "1", 0, 10 );
    g_sink += asprintf( 0, "x" );
    g_sink += (long)strtok_r( 0, 0, 0 );

    /* The Dos* results need interpreting: those live in the OS/2 DLLs, so an
       undefined reference here can mean the linker was not given the import
       library rather than the call not existing.  It is still worth knowing
       which of them the plain link resolves on its own. */
    g_sink += (long)_findfirst( "x", 0, 0 );
    g_sink += _findnext( 0, 0 );
    g_sink += _findclose( 0 );
    g_sink += _chmod( "x", 0 );
    g_sink += (long)_getcwd( scratch, sizeof( scratch ) );
    g_sink += _mkdir( "x" );
    g_sink += _stat( "x", 0 );
    g_sink += _snprintf( scratch, sizeof( scratch ), "x" );
    g_sink += _vsnprintf( scratch, sizeof( scratch ), "x", 0 );
    g_sink += _timezone;
    g_sink += DosCreateThread( 0, 0, 0, 0, 0, 0 );
    g_sink += DosWaitChild( 0, 0, 0, 0 );
    g_sink += DosCreateMutexSem( 0, 0, 0, 0 );
    g_sink += DosRequestMutexSem( 0, 0 );
    g_sink += DosCreateEventSem( 0, 0, 0, 0 );
    DosSleep( 0 );
    g_sink += DosQuerySysInfo( 0, 0, 0, 0 );

    printf( "linked: %ld\n", g_sink );
    return 0;
}
