/* kprobe.cpp -- what does this OS/2 kernel + Watcom RTL actually do?
 *
 * Development probe, not part of the program: `build.cmd link kprobe.cpp
 * kprobe.exe`, run it, read what it prints.  Every line answers a question the
 * compat headers in compat/os2/include have to assume an answer to, and none of
 * those answers is written down anywhere I can read from here.
 *
 * What it asks, and what the first run (19 Oct 2026) answered:
 *
 *   - macros: __WATCOMC__=1290, __OS2__ and __FLAT__ defined, __NT__ and __MT__
 *     not, sizeof(long long)=8 -- and __cplusplus is **1**, not 199711L, which
 *     switches off every `__cplusplus >= 199711L` branch in tvision's headers;
 *   - private (unnamed) semaphores with ulConfFlags == 0: create/post/wait/reset
 *     all behave, a timed wait returns 640, and DosRequestMutexSem is recursive
 *     for the owning thread (so a compat std::mutex built on it is more
 *     permissive than the standard, not less);
 *   - DosTmrQueryFreq = 1193167 Hz and DosTmrQueryTime works, which is what
 *     chrono::steady_clock needs;
 *   - threads: **DosCreateThread fails with 87**, both with the default link and
 *     with mmpm2.lib added, which is why the second half of this program tries
 *     the other two spellings of "make a thread" the kernel has.
 *
 * Two traps this file stepped in and now documents: Watcom evaluates a call's
 * arguments right to left, so putting two API calls in one printf() runs them in
 * the opposite order to the text and reports their results under each other; and
 * <os2.h> declares the 16-bit five-argument DosCreateThread, while a 32-bit
 * process has to call the six-argument one.
 */
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>

/* Two ways for a new thread to find its work: an argument, and a global the
   creator sets before creating it.  A std::thread replacement needs one of the
   two, so both are reported. */
static void *g_job = 0;

static void APIENTRY entryArg( ULONG param )
{
    void *p = malloc( 4096 );
    printf( "    [thread] argument 0x%lx, g_job 0x%lx (want 0xbeef), malloc %s\n",
            param, (unsigned long) g_job, p ? "ok" : "FAILED" );
    fflush( stdout );
    free( p );
    DosExit( EXIT_THREAD, 7 );
}

/* The 32-bit flavour takes no argument at all. */
static void APIENTRY entryNoArg( void )
{
    void *p = malloc( 4096 );
    printf( "    [thread] no argument; g_job 0x%lx (want 0xbeef), malloc %s\n",
            (unsigned long) g_job, p ? "ok" : "FAILED" );
    fflush( stdout );
    free( p );
    DosExit( EXIT_THREAD, 7 );
}

/* Six arguments: ptid, start, stack address, stack free, stack maximum,
   priority.  Cast the import thunk, because the header's prototype is the other
   arity and calling through a prototype that disagrees is not a warning worth
   trusting. */
typedef APIRET (APIENTRY *CreateThread6)( PTID, void (APIENTRY *)( void ),
                                          PVOID, ULONG, ULONG, ULONG );

int main()
{
    APIRET rc;
    ULONG  old = 999;
    TID    tid = 0;

    printf( "== macros ==\n" );
    printf( "__WATCOMC__=%d\n", __WATCOMC__ );
#ifdef __cplusplus
    printf( "__cplusplus=%ld\n", (long) __cplusplus );
#else
    printf( "__cplusplus: not defined\n" );
#endif
    printf( "__OS2__=%d __FLAT__=%d __MT__=%d\n",
#ifdef __OS2__
            1,
#else
            0,
#endif
#ifdef __FLAT__
            1,
#else
            0,
#endif
#ifdef __MT__
            1
#else
            0
#endif
          );
    printf( "sizeof(long)=%u sizeof(long long)=%u sizeof(void*)=%u\n",
            (unsigned) sizeof( long ), (unsigned) sizeof( long long ),
            (unsigned) sizeof( void * ) );

    printf( "== mutex semaphore, unnamed, flags 0 ==\n" );
    {
        HMTX mtx = 0;
        APIRET created, requested, released, again;
        created = DosCreateMutexSem( 0, &mtx, 0, 0 );
        printf( "  create rc=%lu handle=%lu\n", created, mtx );
        if( created == 0 )
        {
            requested = DosRequestMutexSem( mtx, SEM_INDEFINITE_WAIT );
            released  = DosReleaseMutexSem( mtx );
            printf( "  request rc=%lu release rc=%lu\n", requested, released );
            /* Holding it once and asking again: 0 means the kernel lets the
               owner in twice, i.e. it is recursive. */
            requested = DosRequestMutexSem( mtx, SEM_INDEFINITE_WAIT );
            again     = DosRequestMutexSem( mtx, 250 );
            printf( "  held by us (rc=%lu), ask again rc=%lu (0 = recursive, 621 = not)\n",
                    requested, again );
            DosReleaseMutexSem( mtx );
            DosCloseMutexSem( mtx );
        }
    }

    printf( "== event semaphore, unnamed, flags 0 ==\n" );
    {
        HEV ev = 0;
        APIRET created, waited, posted, reset;
        created = DosCreateEventSem( 0, &ev, 0, FALSE );
        printf( "  create rc=%lu handle=%lu\n", created, ev );
        if( created == 0 )
        {
            waited = DosWaitEventSem( ev, 200 );
            printf( "  wait before any post rc=%lu (640 = timed out, right)\n", waited );
            posted = DosPostEventSem( ev );
            waited = DosWaitEventSem( ev, 1000 );
            reset  = DosResetEventSem( ev, &old );
            printf( "  post rc=%lu wait rc=%lu reset rc=%lu post count was %lu\n",
                    posted, waited, reset, old );
            /* Auto-reset on create, which is what a condition variable wants:
               one post, one waiter woken, and the state clears by itself. */
            DosCloseEventSem( ev );
        }
    }

    printf( "== timer ==\n" );
    {
        ULONG freq = 0;
        unsigned long long t1 = 0, t2 = 0;
        ULONG slept;
        rc = DosTmrQueryFreq( &freq );
        printf( "  freq rc=%lu value=%lu\n", rc, freq );
        DosTmrQueryTime( (PQWORD) &t1 );
        DosSleep( 250 );
        DosTmrQueryTime( (PQWORD) &t2 );
        slept = (ULONG) (t2 - t1) / (freq / 1000);
        printf( "  250 ms sleep measured as %lu ms (ticks %lu..%lu)\n",
                slept, (unsigned long) t1, (unsigned long) t2 );
    }

    printf( "== threads ==\n" );
    {
        CreateThread6 create6 = (CreateThread6) DosCreateThread;
        char *stack = (char *) malloc( 65536 );

        tid = 0;
        g_job = (void *) 0xbeef;
        rc = DosCreateThread( &tid, (PFNTHREAD) entryArg, 0x1234UL, 65536, 10 );
        printf( "  header's 5-argument form: rc=%lu tid=%lu\n", rc, tid );
        DosSleep( 400 );

        tid = 0;
        rc = create6( &tid, entryNoArg, 0, 0, 65536, 10 );
        printf( "  6-argument form, stack from the system: rc=%lu tid=%lu\n", rc, tid );
        DosSleep( 400 );

        tid = 0;
        rc = create6( &tid, entryNoArg, stack, 65536, 65536, 10 );
        printf( "  6-argument form, 64 KB heap stack: rc=%lu tid=%lu\n", rc, tid );
        DosSleep( 400 );

        {
            THREADCREATE tc;
            tc.cbSize  = sizeof( tc );
            tc.pTid    = &tid;
            tc.pfnStart = (PFNTHREAD) entryArg;
            tc.lParam  = 0x1234UL;
            tc.lFlag   = 0;
            tc.pStack  = 0;
            tc.cbStack = 65536;
            rc = DosCreateThread2( &tc );
            printf( "  DosCreateThread2, system stack: rc=%lu tid=%lu\n", rc, tid );
            DosSleep( 400 );

            tc.pStack = (PBYTE) stack;
            tid = 0;
            rc = DosCreateThread2( &tc );
            printf( "  DosCreateThread2, 64 KB heap stack: rc=%lu tid=%lu\n", rc, tid );
            DosSleep( 400 );
        }
        free( stack );
    }

    printf( "KPROBE-DONE\n" );
    return 0;
}
