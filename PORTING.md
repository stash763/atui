# atui Porting Notes

How atui is written for the platforms it has to run on: Linux, FreeBSD, Windows,
OS/2 and DOS, with the compilers those systems actually have.

Measured facts -- toolchain behaviour, kernel and stack call spellings, sizes, what
the target answered -- are in `TESTING.md`. The permission model is in
`PERMISSIONS.md`. This file is the part that is about *shape*: what has to be true of
the code so that one program runs on all of them, and what that deliberately does not
give up.

## Threading: what the platforms have

| Platform | Threads | What atui does today |
|---|---|---|
| Linux, FreeBSD | pthreads | pumped, not threaded |
| Windows | Win32 threads (`_beginthreadex`), CRT threaded runtime | pumped, not threaded |
| OS/2 | **in use**: `_beginthread` creates real threads, and `-bm` is not what enables it -- the OS/2 32-bit C library carries the threading machinery already and installs its heap/file locks when the first thread starts (measured both ways in `TESTING.md`) | **threaded**: a turn runs on a worker thread, the UI drains the queue |
| DOS | no preemptive threads | one stack, several tasks, cooperatively -- which is what the pump is |

So: **no platform atui targets forbids threads, and two of them have nothing else to
use instead of a pump.** The design has to allow a threaded host and must not require
one. That is the constraint the rest of this file is about.

The language level is C++98 in `src/`, which also means no C++11 *library*. Open
Watcom ships none of `<mutex>`, `<condition_variable>`, `<chrono>`, `<thread>` or
`<atomic>` -- the includes fail with E059 -- so `compat/os2/include/` supplies what
the program needs over the kernel's own primitives: `mutex` is OS/2 mutex semaphores,
`condition_variable` is an OS/2 event semaphore built to avoid the lost wakeup, and
`chrono` is there too.

`<thread>` and `<atomic>` are **not** supplied, and in the end that cost nothing: the
seam below is what atui uses, so the platforms differ in one `.cpp` file and the
language level never changes. The OS/2 target is threaded now, with `_beginthread`, and
without `-bm` -- `TESTING.md` has the measurement and the reason the flag was never the
gate.

`tools/gate98.py` is what keeps the language level honest; the compat directory is
what keeps the library level honest.

## The rule: work is a state machine, and who runs it is not part of it

Everything in atui that takes longer than a keystroke is an object with this shape:

```cpp
bool start( ... );        // returns false if it could not begin, with a reason
void step( int waitMs );  // at most one wait; moves whatever bytes/bytes-of-work
bool busy() const;
void abort( const std::string &why );
class Sink { ... };       // what the owner hears, called from inside step()
```

`HttpStream`, `SseStream`, `ChatRequest` and `ChatTurn` are all built that way. What
makes it useful is that the owner is not named anywhere in them. Four owners fit:

- **the TUI's idle handler**, which is what the chat path uses: `step(0)` or
  `step(50)` between keystrokes, so the screen draws and a click lands while a model
  thinks;
- **a caller willing to wait** -- `pumpUntilDone(machine, seconds)` in
  `src/net/http_simple.h` -- for the model list and `--judge-check`, which have no
  event loop and should not have a second transport;
- **a cooperative scheduler**: a timer hook that steps several state machines in turn
  is how one stack on DOS does several things at once, and it needs no change to any
  of these objects;
- **a worker thread**, on the platforms that have them: a thread's body is
  `while (machine.busy()) machine.step(50);` and the sink marshals back to whatever
  draws.

Four consequences, and they are the reason the shape is worth its cost:

1. **One pump at a time per object.** Ownership is whoever holds the instance; there
   is no registry of in-flight work for two owners to find. A second request is a
   second object, not a lock on the first.
2. **Sinks return promptly and do not block.** On a pumped host the sink runs on the
   only stack, so a sink that blocks is the UI freezing. A threaded host inherits the
   same rule from the other direction: the sink is where it must hand off to the
   thread that owns the screen.
3. **Inputs are taken at the boundary.** `ChatTurn::start()` copies the endpoint, key,
   model and temperature once, for two reasons: a Providers dialog edited mid-turn
   must not retarget a request already under way, and the object then holds no
   reference into process-wide state while it runs -- which is what makes it movable.
4. **`abort()` is not cancellation.** It closes what can be closed (a socket); a child
   process that is already running is not interruptible, and the turn ends after it.
   That is stated rather than papered over because it is true on every target.

## What is thread-neutral today

Checked rather than assumed:

- **The socket layer** (`src/net/sock_*.cpp`): stack start-up is idempotent and called
  per use (`sock_init()` on OS/2, `WSAStartup()` on Windows), errors come from the call
  that failed rather than a process-wide last-error, and there is no global table of
  sockets -- each `HttpStream` owns its descriptor and closes its own.
- **`HttpStream`, `SseStream`, `ChatRequest`, `ChatTurn`**: per-instance state only --
  no file-scope mutable statics, no locks, no singletons. Each holds its own buffers,
  deadlines and sink pointer.
- **No work is reachable from two places.** Nothing registers a request in a global
  list, so nothing can pick up an object another thread is stepping.
- **Signals are not installed by the transport**, and the one disposition that had to
  change (SIGPIPE, above) is changed per socket rather than for the process, so a
  thread that did not open a socket is not affected by one that did.
- **A stream is constructed with its file or it is not read.** Reading an
  `std::ifstream` that was never given a file returns failure and looks harmless; on OS/2
  with Open Watcom it traps during exit, after the program has printed its correct answer
  (`SYS1808`, exception 0005). `if (!stream)` does not detect it -- an unattached stream
  is not in an error state -- so it has to be avoided by structure. Measured and written up
  in TESTING.md, "An unopened stream is an exit-time crash on Watcom".
- **The only handlers in the program are the three that ask it to stop**
  (`core/signals.h`: SIGHUP, SIGTERM, SIGINT), and a handler sets one
  `volatile sig_atomic_t` and returns. Nothing else may happen there -- no lock, no
  allocation, no write to the terminal -- because a handler runs on whatever
  instruction the signal landed on, and this program has measured the alternative: a
  handler that writes to a terminal nobody is reading never returns. The event loop
  notices the flag on its next tick and quits through the same path Alt-X uses.
  The flag is *not* cleared when the tick acts on it. Leaving is a process rather than an
  instruction -- the teardown joins a worker that may be inside a tool's child process or
  a socket read -- and those waits read the same flag to know they should stop early. A
  tick that forgave the request on its way out left a `sleep 40` to run to completion,
  with the check in place and nothing to see.

## What a threaded host has to guard

The process-wide state is real and unsynchronised today. It is listed because "it
works single-threaded" is not the same as "it is safe", and the next person to add a
thread should not have to find these by hand:

- `net::providerSettings()`, `tools::permissionMode()`, `judgeSeconds()`,
  `networkPolicy()`, `settings::...` -- read at the boundaries; a thread should read
  once and pass values, which is what `ChatTurn::start()` already does for the
  endpoint.
- `tools::ToolRegistry::instance()`, the plan cache and the session-allowance list in
  `tools/judge.cpp`, `tools::projectRoot()`, `core::journal` (a numbered counter, a
  directory, an in-memory entry list). The worker-thread model that used to run the
  chat path read *and wrote* the plan cache from its thread while the UI thread could
  read it through `--judge-check`-style paths: a real race, of the sort that has never
  been reported because it is a wrong cached plan rather than a crash. The pumped path
  does not have it; a threaded path must not reintroduce it.
- Anything that draws. The sink's owner is the thread that owns the screen.
- **Signal dispositions**, which are process-wide and inherited by every command atui
  runs through the shell. The one that matters here is SIGPIPE: writing to a socket
  whose peer has gone raises it, and the default disposition terminates the process.
  Measured (`tools/probe_sigpipe.cpp`): a peer that accepts, hangs up, and is written
  to again killed the process in 6 runs out of 6, printing nothing -- which in the
  program would be an atui that vanished mid-chat rather than an error line. The fix
  is per-socket (`SO_NOSIGPIPE` where that option exists, `MSG_NOSIGNAL` on the send
  where that flag exists), and `SIG_IGN` at startup is deliberately *not* the answer:
  children inherit an ignored SIGPIPE, and that is how `yes | head` starts misbehaving
  through atui. Neither exists on OS/2 or Windows, which have no SIGPIPE at all, so
  the difference has to be handled in the socket layer rather than assumed.
- What a thread does to `run_command`'s child handling: the deadline work already has
  to reason about a process group; two threads reaping is a worse question, so child
  handling should stay on one owner (see `TESTING.md`, "What a command atui runs
  gets").
- `run_command` uses `system()` / `_popen` with a copy of the environment: it inherits
  the process's cwd and environment, and two of them at once is a decision to make
  deliberately, not accidentally.

## Where the work runs: a thread where the platform has one, a timer where it does not

The requirement, stated plainly: **the UI must not be blocked while an HTTP response
is waited for, or while a tool call runs.** That rules out "pumped everywhere" as the
final answer, and it rules it out for a reason a state machine cannot argue away: on
these platforms waiting for a child process has no non-blocking form at all, so a tool
call blocks whatever runs it, and the only way the screen keeps moving is if the thing
being blocked is not the thread that draws.

So the direction is: **run the work on a thread on the platforms that have them, and
emulate the thread where they do not** -- DOS by stepping several state machines from a
timer tick, which is the same cooperative shape the pump already is. Threads were
measured working on the OS/2 target, heap and `printf` and TCP/IP from a child
included, so the platforms divide as `Threading: what the platforms have` above; what
is missing is not a platform's permission but two headers of atui's own.

What that does *not* change is the part worth keeping from the pumped design, and the
distinction matters, because the thing this restructure spent a week deleting was not
"a thread" -- it was **two transports and a handoff between them**:

- **One body of logic.** `HttpStream`, `SseStream`, `ChatRequest`, `ChatTurn` stay
  exactly as they are: `start()`, `step(waitMs)`, `busy()`, `abort()`, a sink. Nobody
  in them knows or cares who calls `step()`.
- **One owner per machine**, chosen by whoever holds it -- and now: a thread.
- **One UI-side path.** The thread's sink calls go into a queue the UI drains on its
  own event loop, and on a platform with no thread the same queue is filled by the
  cooperative step on the UI's own timer. The screen code does not have two versions,
  and neither do the tests.
- **The sink still never blocks**, and it still does not draw: on a threaded host it
  is where the handoff happens; the thread that owns the screen is the one that draws.

### The order to do it in

1. ~~`core/thread.{h,cpp}`~~ **done**: `core/thread.h` with `thread_posix.cpp`,
   `thread_os2.cpp` (`_beginthread` plus an event semaphore for the join),
   `thread_win.cpp` (compiles; not yet run on Windows, and the file says so) and
   `thread_none.cpp`, which is what makes "no threads" a configuration that works
   rather than a link error. No `<atomic>` anywhere: sharing is a mutex and a queue,
   because the mutex atui has on OS/2 is a kernel semaphore and that is enough. Tested
   in three configurations on the host, including one built specifically to have no
   threads, and compile-checked on the target (`TESTING.md`, "The thread seam").
2. ~~`core/task_runner.{h,cpp}`~~ → **`net/turn_queue.{h,cpp}`, and no runner.** What
   turned out to be wrong about the plan is the word "generic": a runner in `core`
   would have to know when a machine is waiting on its owner rather than on a socket,
   and the only honest way for it to learn that is a hook every machine implements --
   which is a framework, and frameworks are how a port ends up with two ways to do a
   thing. So `core` keeps `Thread` (one thread, one join, no opinions), and the pump
   loop lives with the machine that has the states: `while (turn.busy()) { if
   (!queue.waitIfWaiting()) turn.step(50); }`. The queue is the part that is genuinely
   shared and genuinely tested: `net::TurnQueue` implements `ChatTurn::Sink`, appends
   on the worker's stack in order, and carries the approval question both ways -- take
   it once, answer it once, and a question nobody answers ends as a refusal.
3. Put a `ChatTurn` behind it. The visible win is a `run_command` that takes a minute:
   today it stops the screen, and after this it does not.
4. Move the process-wide state out of the way first, in the same change: read once at
   the boundary and pass values, which `ChatTurn::start()` already does for the
   endpoint. The list it has to cover is above under "What a threaded host has to
   guard"; the plan cache is the one that already raced in the old threaded model.
5. ~~Decide `-bm` as a build configuration~~ **Decided against, by measurement.** The
   OS/2 build defines `ATUI_HAVE_OS2_THREADS` and does not pass `-bm`: with and without
   it, two children ran 100,000 concurrent `new`/`delete` pairs and finished clean, and
   `__MT__` is defined by neither. The flag would cost a rebuilt tvision -- 177 objects
   that all have to agree about the run time -- for nothing measured.
6. **The wake-up, settled by measurement rather than by design** (`TESTING.md`, "The
   transcript was the freeze"). The UI thread on the box looked like a thread that was
   never woken -- 317 seconds without a timer event -- and the fix that would have
   followed from that guess was a wakeup channel into tvision's input wait. Measured,
   the input wait returns when asked (20 calls a second, none over 70 ms) and the timer
   fires at ~20 Hz; what held the UI thread for 317 seconds was the transcript's own
   layout, quadratic in the conversation because every streamed chunk re-wrapped and
   repainted the whole document. That is fixed, and a record now reaches the screen
   within one tick on every platform, so **no wakeup pipe is needed and none was built**
   -- on OS/2 there is nothing to wake anyway, since readiness is `KbdPeek()` on a
   console rather than a descriptor a `select()` could watch.
   What the rule comes down to is the two that the measurements actually support:
   `pumpUntilDone()` never runs on the UI thread, and a UI path that costs more than a
   tick is a bug on the slowest target even when it is invisible on the fastest.

`-DATUI_HAVE_THREADS` is the only place the platforms differ, and the state machines
never test it. That is the test the design has to pass: a machine that can be stepped
by a thread and by a timer without either one being mentioned in it.

## The screen on OS/2: a VT stream, not VIO, and stated rather than measured

Two rules about the display on the target, both of them consequences of one decision, and
both of them things a person will otherwise discover as a bug.

**atui talks to the screen with VT escapes on stdout; it does not use VIO.** VIO was tried
first and measured: every `Vio*` entry point resolves in `os2386.lib` and then fails in a
process the telnet server started -- `ERROR_VIO_INVALID_HANDLE` (436) -- and `KbdPeek`
fails with 445. A program whose job is to be reached over the network cannot depend on the
console the machine owns while somebody is sitting at it. The cost is stated in the driver
(`tvision/source/tvision/hardwvos2.cpp`) and is not small: **OS/2's own full-screen session
is not a VT terminal, so on the box's own display atui does not render.** Reading the
escape sequences scroll past is what that looks like. A VIO driver for the machine's own
screen is separate port work, and it belongs beside this one rather than replacing it: the
telnet case is the one the program exists for.

**The size is stated, not discovered.** `ATUI_COLS` and `ATUI_ROWS` are the size, and
80x25 is what they default to. Nothing asks the terminal -- a stream cannot, and nothing
tells a stream when its window was resized -- so a window of any other size shows an 80x25
island in its top-left corner, which is correct behaviour and looks exactly like a bug:

    [<P4>-D:\atui]set ATUI_COLS=100& set ATUI_ROWS=40& atui.exe

fills a 100x40 window (measured: separator across 98 columns, status line on row 40).
Querying the terminal with `ESC [ 18 t` is the way to stop needing the two variables; it
has not been done, and it has a cost worth thinking about first -- the reply arrives on
the same stdin as the keystrokes, so reading it means swallowing bytes that might be a
key the user already pressed.

## A legacy terminal driver writes every cell it is handed

`tvision/source/tvision/tvwrite.cpp` calls `THardwareInfo::screenWrite()` for the run a
view just drew, and it does not compare anything first. So on the platforms that use the
classic layer -- OS/2 and DOS above all, because they cannot build the modern terminal
code that does its own diffing -- **a repaint is output**. Two rules follow, and both were
learned by getting them backwards (the measurements are in TESTING.md, "The separator that
redrew itself forever"):

*   **Draw each cell at most once per pass.** The shape `blank the row, then write the text
    over it` sends both halves, because nothing downstream can tell that the second write
    undoes the first. Padding the text out to the width of the row does the same job in one
    write and erases correctly when the text gets shorter.
*   **Do not skip a repaint because nothing *you* changed.** A view does not own the screen:
    a sibling or the desktop can paint over it, and a view that decided it had nothing new
    to draw leaves a hole. Skipping has to happen where the truth is -- in the driver, which
    is the only thing that knows what the terminal was last sent. That is what its shadow
    buffer is for, and its one weakness is worth knowing: the shadow is a claim about the
    terminal, so anything that changes the screen behind the driver's back (a program
    sharing the terminal, a window resized) leaves the display wrong until something
    repaints those cells.
