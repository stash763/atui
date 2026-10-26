/* prof.c -- a sampling CPU profiler for the OS/2 target, built for one question:
   which thread of atui is burning the machine, and where in the program.

   Why this exists when Open Watcom ships wsample: wsample runs the target as a
   traced child and, as shipped (bld/wsample/c/sampos22.c in the OW sources),
   terminates it the moment *any* first-chance exception is reported.  A
   multithreaded OS/2 program raises those routinely -- the guard page on a new
   thread's stack, faults the TCP/IP stack handles internally -- so profiling
   atui under wsample ended with "CPU exception reported" about ten milliseconds
   into a turn, right where the worker thread starts, and no trace file at all
   (measured on the P4 box, 2026-10-25: the child died, the parent hung before
   writing, and the session went with them).  A sampler must answer first-chance
   exceptions with "not handled" and keep sampling; that is the design decision
   this file exists to make, and the toolkit header `bsexcpt.h` states the rest:
   XCPT_CONTINUE_SEARCH means the program's own handlers get first refusal,
   which is exactly what a passive observer wants.

   Why not patch wsample itself: its OS/2 build is nine objects plus OW's
   message subsystem, internal headers (`bool.h`, `clibint.h`, `pathgrp2.h`)
   and generated `.gh` files -- all of a 2.0-beta tree, to be backported onto
   the Watcom 1.9 install on this box.  The DosDebug mechanics are not the hard
   part and the toolkit documents them; everything else is scaffolding a sampler
   does not need.

   The trace file is plain text, one event per line, flushed as it goes so a
   crashed run leaves whatever it reached:

       ATUIPROF 1
       RATE <ms>
       RUN <pid> <program>
       NEWPROC <eip-hex> <mte-hex>
       MODULE <mte-hex> <name>
       THREAD <tid> / THREADEND <tid>
       SAMPLE <tid> <eip-hex>
       EXC <state> <number-hex> <fault-addr-hex> <eip-hex> <tid>
       OBJSCAN <seq>
       OBJ <num> <addr-hex> <mte-hex> <index>
       STATS exceptions <n> firstchance <n>
       END <reason>

   The notification layout and the command codes come from Open Watcom's own
   OS/2 debugger backend (bld/trap/lcl/os2/h/dosdebug.h and c/os2v2acc.c): at
   DBG_N_Exception, Value is the DBG_X_* state, Buffer carries the exception
   number for the pre-first-chance notification, and Addr the fault address.
   Modules are named from DosQueryModuleName on each load notification, and the
   debugger's object table (DBG_C_NumToAddr) is re-walked at every load so the
   reader can attribute addresses to modules by the difference between scans.

   Build on the box (this file syncs to the work dir by tools/atui-remote):

       build.cmd link prof.c prof.exe

   Use:

       prof -r20 run.trc candidate7.exe --ask "tell me about something"

   The child inherits this process's environment and session, so atui still
   finds the model endpoint and, over a telnet session, still draws through the
   ANSI stream path; a local-screen run is launched the same way at the screen.
   Host-side reader: tools/wsample_report.py, which skips line types it does
   not know.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* INCL_DOS arrives from the build's defs.rsp, same as every other file on this
   target; these two are the ones the APIs below add. */
#define INCL_DOSPROCESS
#define INCL_DOSMODULEMGR
#include <os2.h>

/* ---------------------------------------------------------------------------
   The debugger interface.

   DosDebug is old enough that headers and import libraries are not guaranteed
   to agree across versions, so the notification buffer is defined here field
   for field, at the offsets dosdebug.h documents.  A sampler touches little of
   it -- Pid, Cmd, Value, and the interrupted thread's Tid and EIP -- but the
   layout has to be exact for those to land where the kernel puts them. */

typedef struct {
/*  0 */ unsigned long   Pid;
/*  4 */ unsigned long   Tid;
/*  8 */ long            Cmd;
/*  C */ long            Value;
/* 10 */ unsigned long   Addr;
/* 14 */ unsigned long   Buffer;
/* 18 */ unsigned long   Len;
/* 1C */ unsigned long   Index;
/* 20 */ unsigned long   MTE;
/* 24 */ unsigned long   EAX;
/* 28 */ unsigned long   ECX;
/* 2C */ unsigned long   EDX;
/* 30 */ unsigned long   EBX;
/* 34 */ unsigned long   ESP;
/* 38 */ unsigned long   EBP;
/* 3C */ unsigned long   ESI;
/* 40 */ unsigned long   EDI;
/* 44 */ unsigned long   EFlags;
/* 48 */ unsigned long   EIP;
/* 4C */ unsigned long   CSLim;
/* 50 */ unsigned long   CSBase;
/* 54 */ unsigned char   CSAcc;
/* 55 */ unsigned char   CSAtr;
/* 56 */ unsigned short  CS;
/* 58 */ unsigned long   DSLim;
/* 5C */ unsigned long   DSBase;
/* 60 */ unsigned char   DSAcc;
/* 61 */ unsigned char   DSAtr;
/* 62 */ unsigned short  DS;
/* 64 */ unsigned long   ESLim;
/* 68 */ unsigned long   ESBase;
/* 6C */ unsigned char   ESAcc;
/* 6D */ unsigned char   ESAtr;
/* 6E */ unsigned short  ES;
/* 70 */ unsigned long   FSLim;
/* 74 */ unsigned long   FSBase;
/* 78 */ unsigned char   FSAcc;
/* 79 */ unsigned char   FSAtr;
/* 7A */ unsigned short  FS;
/* 7C */ unsigned long   GSLim;
/* 80 */ unsigned long   GSBase;
/* 84 */ unsigned char   GSAcc;
/* 85 */ unsigned char   GSAtr;
/* 86 */ unsigned short  GS;
/* 88 */ unsigned long   SSLim;
/* 8C */ unsigned long   SSBase;
/* 90 */ unsigned char   SSAcc;
/* 91 */ unsigned char   SSAtr;
/* 92 */ unsigned short  SS;
/* 94 */ unsigned short  segv;
} DebugBuf;

#ifndef DBG_C_Stop
#define DBG_C_Go              7
#define DBG_C_Term            8
#define DBG_C_Stop            10
#define DBG_C_NumToAddr       13
#define DBG_C_Connect         21
#define DBG_C_ReadMemBuf      22
#endif
#ifndef DBG_N_AsyncStop
#define DBG_N_Success         0L
#define DBG_N_Error           (-1L)
#define DBG_N_ProcTerm        (-6L)
#define DBG_N_Exception       (-7L)
#define DBG_N_ModuleLoad      (-8L)
#define DBG_N_ThreadTerm      (-10L)
#define DBG_N_AsyncStop       (-11L)
#define DBG_N_NewProc         (-12L)
#define DBG_N_ThreadCreate    (-15L)
#define DBG_N_ModuleFree      (-16L)
#endif
#define DBG_L_386             1

/* Exception states, in Value with DBG_N_Exception (dosdebug.h). */
#define DBG_X_PRE_FIRST_CHANCE 0
#define DBG_X_FIRST_CHANCE     1
#define DBG_X_LAST_CHANCE      2
#define DBG_X_STACK_INVALID    3

/* Dispositions, bsexcpt.h.  A sampler only ever says SEARCH -- "the program's
   handlers go first" -- and stops watching at LAST_CHANCE, where the program
   really is dying.  The installed headers already carry them under slightly
   different spellings, hence the guards. */
#ifndef XCPT_CONTINUE_SEARCH
#define XCPT_CONTINUE_SEARCH      0x00000000UL
#endif
#ifndef XCPT_CONTINUE_EXECUTION
#define XCPT_CONTINUE_EXECUTION   0xFFFFFFFFUL
#endif

#ifndef ERROR_INVALID_PROCID
#define ERROR_INVALID_PROCID  87
#endif

/* The entry point itself: a plain DOSCALLS function; if the headers predate
   it, this declaration is enough and os2386.lib resolves the name. */
extern APIRET APIENTRY DosDebug(void *dbgBuf);

/* ---------------------------------------------------------------------------
   State. */

static FILE *gOut   = 0;
static long  gRate  = 50;             /* ms between samples */
/* Sampling budget, ms.  Zero samples forever, which races the child's death:
   a DBG_C_Stop that arrives while the kernel is tearing the debug session
   down wedges both the command and the session-close behind a teardown
   timeout -- about ten minutes of nothing measured on this box twice
   (2026-10-26: wsample's hang after killing its child, and prof's hang after
   END term, same duration).  A profiler can simply stop before that moment:
   with a budget of B the sleeper goes silent at B and the child then lives
   out its remaining time unsampled and dies in peace, ProcTerm arrives on
   schedule, and prof exits immediately.  Pair it with the child's own
   ATUI_QUIT_AFTER_MS: budget = quit-after - 2000. */
static long  gBudget = 0;
static ULONG gPid   = 0;
static long  gLines = 0;
/* The event loop sets this before it stops listening.  Without it, the sleeper
   keeps calling DosDebug against a debug relationship the kernel has already
   closed -- and DosDebug on a dead debuggee does not reliably return, which is
   how a run that ended cleanly (END term in the trace, every flush complete)
   left the command processor waiting three hundred seconds for a prompt that
   was never going to come (measured on the box, 2026-10-26). */
static volatile long gDone = 0;

/* The box's wall clock, printed on the trace's first and last lines: the
   difference between a run's end here and the child's own last word in its
   debug log is how a ten-minute question gets answered without guesswork. */
static void stamp(FILE *f)
{
    DATETIME dt;
    DosGetDateTime(&dt);
    fprintf(f, "%02u:%02u:%02u", dt.hours, dt.minutes, dt.seconds);
}

static void emit(const char *fmt, ...)
{
    /* Flushing every 64 lines keeps a killed run's trace readable without
       paying for a kernel call per sample at 20 Hz. */
    va_list ap;
    va_start(ap, fmt);
    vfprintf(gOut, fmt, ap);
    va_end(ap);
    if (++gLines % 64 == 0)
        fflush(gOut);
}

/* Walk the debugger's object table -- "object number i, what is your
   address?" -- and write every entry.  The table is process-wide and grows as
   modules load; scanning it at every load lets the reader attribute each new
   object to the module whose load triggered the scan.  It is not a big table
   (tens of entries) and module loads are rare, so an honest full walk each
   time beats any bookkeeping. */

static void scanObjects(DebugBuf *g, long seq)
{
    long i;
    emit("OBJSCAN %ld\n", seq);
    for (i = 1; i < 4096; ++i) {
        g->Pid   = gPid;
        g->Cmd   = DBG_C_NumToAddr;
        g->Value = (unsigned long) i;
        if (DosDebug(g) != 0 || g->Cmd != DBG_N_Success)
            break;
        emit("OBJ %ld %lx %lx %lu\n", i, g->Addr, g->MTE, g->Index);
    }
    fflush(gOut);
}

/* The sampler: stop the child every gRate ms.  The resulting notification --
   carrying the interrupted thread's EIP -- arrives as the return of the main
   loop's pending wait.  Time-critical priority so the interval does not depend
   on the child being scheduled; an observer that starves its subject is bad,
   but one that never fires is worse. */

static void APIENTRY Sleeper(unsigned long arg)
{
    DebugBuf s;
    (void)arg;
    long t = 0;
    for (;;) {
        DosSleep(gRate);
        if (gDone)
            return;
        t += gRate;
        if (gBudget > 0 && t > gBudget)
            return;                   /* the silence: no Stop after this point */
        memset(&s, 0, sizeof s);
        s.Pid = gPid;
        s.Cmd = DBG_C_Stop;
        DosDebug(&s);
        /* A failing stop is normal -- the child may have died between the
           sleep and here.  The main loop sees the death through its own wait
           either way; this only stops the metronome ringing forever. */
        if (s.Cmd == DBG_N_Error && s.Value == ERROR_INVALID_PROCID)
            return;
    }
}

/* ---------------------------------------------------------------------------
   Starting the child: EXEC_TRACE stops the new process at its entry point
   waiting for its debugger -- us.  See launch() below for the argument
   block's real shape -- it is not an argv array, and getting that wrong
   silently loses every argument after the first. */

static char gArgs[2048];

static ULONG launch(const char *prog, int argc, char **argv, int first)
{
    RESULTCODES res;
    char *p;
    int   i, room;

    /* DosExecPgm's argument block is NOT an argv array: it is the program
       path, a NUL, then the command tail as ONE raw string, terminated by a
       final NUL (sampos22.c's LoadProg builds exactly this).  The child's C
       runtime re-tokenizes that tail on spaces -- a NUL-delimited argument,
       however correct it looks, simply ends the line early (measured: a
       multi-word --ask question arrived as `--ask` alone, "needs a
       question").  So: join with spaces and re-quote any argument that
       needs it, which is the convention the runtime honors. */
    memset(gArgs, 0, sizeof gArgs);
    p = gArgs;
    room = (int) sizeof gArgs - 1;
#define PUT(c) do { if (room <= 0) { fprintf(stderr, \
        "prof: arguments too long\n"); return 0; } *p++ = (c); --room; } while (0)
    while (*prog)
        PUT(*prog++);
    PUT('\0');
    for (i = first; i < argc; ++i) {
        const char *a = argv[i];
        if (i > first)
            PUT(' ');
        if (strpbrk(a, " \t") != 0) {
            PUT('"');
            while (*a)
                PUT(*a++);
            PUT('"');
        } else {
            while (*a)
                PUT(*a++);
        }
    }
    PUT('\0');                           /* the second terminating NUL */
#undef PUT

    if (DosExecPgm(NULL, 0, EXEC_TRACE, gArgs, NULL, &res, gArgs) != 0) {
        fprintf(stderr, "prof: cannot start %s\n", prog);
        return 0;
    }
    /* bsedos.h's RESULTCODES carries the pid in codeTerminate when the exec
       flag is EXEC_TRACE -- the union member sampos22 reads too. */
    return (ULONG) res.codeTerminate;
}

/* ---------------------------------------------------------------------------
   The event loop -- which is the sampler.

   Every wait after the first is DBG_C_Continue carrying XCPT_CONTINUE_SEARCH,
   the shape Open Watcom's own OS/2 debugger runs (os2v2acc.c issues Continue
   with a disposition for every notification): the call answers whatever is
   pending -- a stop, a first-chance exception -- resumes the child, and blocks
   until the next notification.  One uniform answer means there is never a
   pending exception sitting unanswered while some other wait is issued, which
   is a hang waiting to happen the moment the sleeper's stop and a guard page
   race each other. */

int main(int argc, char **argv)
{
    DebugBuf   g;
    int        i = 1, firstArg;
    char      *trace, *prog;
    TID        sleeper;
    long       exceptions = 0, firstChance = 0, scan = 0;
    long       cmd, value;

    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'r') {
        long r = atol(argv[1] + 2);
        if (r < 1 || r > 1000) {
            fprintf(stderr, "prof: rate out of range\n");
            return 2;
        }
        gRate = r;
        ++i;
    }
    if (argc > i && argv[i][0] == '-' && argv[i][1] == 'b') {
        long b = atol(argv[i] + 2);
        if (b < 1) {
            fprintf(stderr, "prof: budget out of range\n");
            return 2;
        }
        gBudget = b;
        ++i;
    }
    if (argc - i < 2) {
        fprintf(stderr,
            "usage: prof [-r<ms>] [-b<ms>] <trace.trc> <program> [arguments]\n"
            "  -r<ms>  sampling interval (default 50)\n"
            "  -b<ms>  sampling budget: go silent after this long, before the\n"
            "          child's death (0 = sample until the end; races teardown,\n"
            "          see the file header). With ATUI_QUIT_AFTER_MS=N, use N-2000\n");
        return 2;
    }
    trace    = argv[i++];
    prog     = argv[i++];
    firstArg = i;

    gOut = fopen(trace, "wb");
    if (!gOut) {
        fprintf(stderr, "prof: cannot open %s\n", trace);
        return 2;
    }

    gPid = launch(prog, argc, argv, firstArg);
    if (!gPid)
        return 2;

    memset(&g, 0, sizeof g);
    g.Pid   = gPid;
    g.Cmd   = DBG_C_Connect;
    g.Value = DBG_L_386;
    if (DosDebug(&g) != 0) {
        fprintf(stderr, "prof: cannot become %lu's debugger\n", gPid);
        fclose(gOut);
        return 2;
    }

    emit("ATUIPROF 1\nRATE %ld\nBUDGET %ld\nRUN %lu %s ", gRate, gBudget,
         gPid, prog);
    stamp(gOut);
    emit("\n");
    fflush(gOut);

    /* Stop at the entry point and take the first notification (NewProc): the
       process is not running yet, so the initial object table can be walked
       before the loader moves anything. */
    memset(&g, 0, sizeof g);
    g.Pid = gPid;
    g.Cmd = DBG_C_Stop;
    DosDebug(&g);
    emit("NEWPROC %lx %lx\n", g.EIP, g.MTE);
    scanObjects(&g, scan++);

    /* The kernel enters a thread's start address _system-style (argument in a
       register), which is what APIENTRY names on this target; the cast only
       says so to the compiler. */
    if (DosCreateThread(&sleeper, Sleeper, 0, 0, 8192) != 0) {
        fprintf(stderr, "prof: cannot create the sleeper thread\n");
        fclose(gOut);
        return 2;
    }
    DosSetPriority(PRTYS_THREAD, PRTYC_TIMECRITICAL, 0, sleeper);

    cmd   = DBG_C_Go;                   /* the first wait starts it running */
    value = 0;
    for (;;) {
        g.Pid   = gPid;
        g.Cmd   = cmd;
        g.Value = value;
        if (DosDebug(&g) != 0) {
            /* The debug relationship is gone: the child died without
               announcing itself.  The trace ends where it ends. */
            emit("END dosdebug\n");
            break;
        }
        cmd   = DBG_C_Continue;
        value = (long) XCPT_CONTINUE_SEARCH;

        switch (g.Cmd) {
        case DBG_N_AsyncStop:
            emit("SAMPLE %lu %lx\n", g.Tid, g.EIP);
            break;
        case DBG_N_NewProc:
            emit("NEWPROC %lx %lx\n", g.EIP, g.MTE);
            break;
        case DBG_N_ThreadCreate:
            emit("THREAD %lu\n", g.Tid);
            break;
        case DBG_N_ThreadTerm:
            emit("THREADEND %lu\n", g.Tid);
            break;
        case DBG_N_ModuleLoad: {
            char name[260];
            name[0] = '\0';
            if (DosQueryModuleName((HMODULE) g.Value, sizeof name, name) != 0)
                strcpy(name, "?");
            emit("MODULE %lx %s\n", g.Value, name);
            scanObjects(&g, scan++);
            break;
        }
        case DBG_N_Exception: {
            /*
             * Exception policy of a passive observer: never judge, never kill.
             *
             * The states ask different things, and the answers come from the
             * OS/2 debug interface as Open Watcom's own debugger backend uses
             * it (bld/trap/lcl/os2v2/c/os2v2acc.c): Buffer carries the number
             * at PRE_FIRST_CHANCE and a pointer to the child's
             * EXCEPTIONREPORTRECORD at the other states; Addr is the faulting
             * linear address.  Reporting at FIRST chance bypasses the child's
             * own handler chain entirely -- and answering LAST chance with
             * SEARCH is what hands the exception to that chain for its final
             * word, which is where the C runtime's stub handler (stubl.lib)
             * eats the startup notification.  Measured with probe_busy
             * 2026-10-26: stopping at LAST, as an earlier version did, kills
             * every Watcom program that opens with the software-fault banner
             * handshake (0x60068), because the handler swallows it at exactly
             * that moment.  If nothing handles an exception, the kernel ends
             * the process and ProcTerm arrives; that is the only way this
             * loop ends on a fault.
             *
             * Debugger-directed notifications -- breakpoints, single step --
             * belong to no one here and are swallowed outright, and so is the
             * whole 0x600xxxxx software-fault band: its records are letters
             * from the C runtime to a debugger, and a program linked without
             * stubl.lib has no handler to receive them (candidate7 is such a
             * program).  The record is read only to learn the number; the
             * child is stopped so the read is safe, and if it fails the
             * number stays zero and the pass-through answer is still right.
             */
            unsigned long state = (unsigned long) g.Value;
            unsigned long num   = 0;
            ++exceptions;
            if (state == DBG_X_PRE_FIRST_CHANCE)
                num = g.Buffer;
            else if (g.Buffer > 0x1000) {
                /* EXCEPTIONREPORTRECORD: Record, Next, Source, ExceptionNum */
                unsigned long rec[4];
                DebugBuf r;
                rec[0] = rec[1] = rec[2] = rec[3] = 0;
                memset(&r, 0, sizeof r);
                r.Pid    = gPid;
                r.Cmd    = DBG_C_ReadMemBuf;
                r.Addr   = g.Buffer;
                r.Buffer = (unsigned long) rec;
                r.Len    = sizeof rec;
                if (DosDebug(&r) == 0 && r.Cmd == DBG_N_Success)
                    num = rec[3];
            }
            emit("EXC %lu %lx %lx %lx %lu\n", state, num, g.Addr, g.EIP, g.Tid);
            if (state == DBG_X_PRE_FIRST_CHANCE)
                ++firstChance;
            if (num == 0x80000003UL || num == 0x80000004UL)
                value = (long) XCPT_CONTINUE_EXECUTION;   /* XCPT_BREAKPOINT /
                                                             XCPT_SINGLE_STEP:
                                                             ours to eat        */
            else if ((num & 0xFFF00000UL) == 0x60000000UL)
                value = (long) XCPT_CONTINUE_EXECUTION;   /* software-fault
                                                             band: runtime
                                                             letters            */
            else
                value = (long) XCPT_CONTINUE_SEARCH;      /* every state, last
                                                             chance included:
                                                             the program's own
                                                             chain decides      */
            cmd = DBG_C_Continue;
            break;
        }
        case DBG_N_ProcTerm:
            emit("END term\n");
            goto done;
        default:
            emit("END event %ld\n", (long) g.Cmd);
            goto done;
        }
    }

done:
    emit("STATS exceptions %ld firstchance %ld ", exceptions, firstChance);
    stamp(gOut);
    emit("\n");
    fflush(gOut);
    fclose(gOut);
    fprintf(stderr, "prof: trace closed, tearing down\n");

    /* Silence the sleeper, and then touch nothing debug-related: the session
       closed with the child, and every command aimed at it after that -- a
       Term from here, a Stop from the sleeper -- is another ticket to the
       teardown timeout.  What is left of the child is nothing, and what is
       left of the session the kernel will reap. */
    gDone = 1;

    /* And leave the way OS/2 means it: EXIT_PROCESS takes the whole process,
       every thread with it -- including a sleeper that is, despite everything
       above, still parked in a kernel call.  Reaching here means the trace is
       closed and flushed; there is nothing left to lose by going directly. */
    fprintf(stderr, "prof: gone\n");
    DosExit(EXIT_PROCESS, 0);
    return 0;
}
