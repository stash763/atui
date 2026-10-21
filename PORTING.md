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
| OS/2 | threads exist (`DosCreateThread`; Watcom has a threaded runtime and `-mt`) | pumped: atui's compat headers carry no `<thread>`/`<atomic>`, and the build is single-threaded |
| DOS | no preemptive threads | one stack, several tasks, cooperatively -- which is what the pump is |

So: **no platform atui targets forbids threads, and two of them have nothing else to
use instead of a pump.** The design has to allow a threaded host and must not require
one. That is the constraint the rest of this file is about.

The language level is C++98 in `src/`, which also means no C++11 *library*: no
`<thread>`, no `<mutex>`, no `<atomic>`. `tools/gate98.py` is what keeps that honest,
and a stray `#include <mutex>` in a file that must compile under Open Watcom is the
kind of thing it does not catch until the port build tries it -- `src/net/dbg.cpp`
carried one, guarded by nothing, until it was moved inside `ATUI_DEBUG_KEYS`.

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

## Why the chat path stays pumped where threads are available

- **One code path, one set of tests.** The 160 self-tests assert the same behaviour on
  the target and on the host. Two transports plus a thread handoff is exactly what
  this restructure spent a week deleting, and it is what would have to be tested twice.
- **A thread buys less than it looks like.** Waiting for a child process has no
  non-blocking form on these targets, so a tool call blocks whatever runs it either
  way; the part that needed to stop blocking was the network, and that is now a state
  machine that yields between bytes on every platform, threads included.
- **Where a thread genuinely pays** is a *second* request while the first waits:
  prefetching the model list, or a judging agent that never delays the screen. That
  fits this design as a second owner of a second `ChatRequest`, and needs no change to
  either class -- which is the test the design has to pass.

Where an object cannot be moved to a thread, that should be written at its door, in
the same sentence as the reason, so the next person is not left to discover it by
trying.
