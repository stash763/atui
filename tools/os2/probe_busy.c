/* probe_busy.c -- a known CPU shape to point prof at before trusting it with
   atui.  A profiler that cannot separate a spin loop from a DosSleep loop is
   not reading the machine.

   Modes:
     spin   ~4 seconds of a volatile counter loop (user code, real CPU)
     sleep  4 seconds of DosSleep (kernel wait, no CPU)
     both   two seconds of each, so one trace must show both shapes

   Build on the box:  build.cmd link probe_busy.c probe_busy.exe
   Profile:           prof -r20 -b4000 busy.trc probe_busy.exe spin

   Expect afterwards: spin's samples inside the program's own code objects
   (0x10000-0x140000 region for this link), sleep's samples in the low shared
   kernel code the blocked threads sit in (a tight cluster around 0xe000).
   Host-side reading lives in tools/wsample_report.py.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INCL_DOS
#define INCL_DOSPROCESS
#include <os2.h>

/* Kernel time in ms the same way everyone on this target does it. */
static ULONG ulongtime(void)
{
    ULONG ms;
    DosQuerySysInfo(QSV_MS_COUNT, QSV_MS_COUNT, &ms, sizeof ms);
    return ms;
}

static void spin_ms(long ms)
{
    volatile long sink = 0;
    long ii;
    ULONG start, now;
    start = ulongtime();
    for (;;) {
        long i;
        for (ii = 0; ii < 20000; ++ii)
            sink += ii;
        now = ulongtime();
        if ((long)(now - start) >= ms)
            break;
    }
}

int main(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "both";

    if (strcmp(mode, "spin") == 0) {
        spin_ms(4000);
    } else if (strcmp(mode, "sleep") == 0) {
        ULONG t;
        for (t = 0; t < 200; ++t)
            DosSleep(20);
    } else {
        spin_ms(2000);
        {
            ULONG t;
            for (t = 0; t < 50; ++t)
                DosSleep(40);
        }
    }
    printf("probe_busy: %s done\n", mode);
    return 0;
}
