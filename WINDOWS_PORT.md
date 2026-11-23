# Windows Port Plan

How atui gets built and tested on Windows, and what it takes to keep the other
targets whole while doing it. This is the plan; `PORTING.md` is the contract the
code has to keep, and `TESTING.md` is where the measurements go.

Scope, in order: **VS2017 / Win32 / Windows 10 first**, then the older Windows
targets. Nothing here changes what the OS/2, DOS, Linux or FreeBSD builds do.

Status: P0-P2 done on the target, and W1-W3 of the cross-platform work landed.
atui.exe builds and links for Win32, and 143 of its 146 self-tests pass; the three
that do not are one cause (symlink containment, `W5`). See "Measured results" below.

## The rules for this work

These are the constraints that make the rest of the plan safe.

1. **macOS is the source of truth.** Every edit happens in `/Users/stash/code/atui`
   on the Mac and is committed there. The Windows tree is a build-and-run copy and
   is disposable at any moment. Nothing is edited on Windows, so nothing can only
   exist on Windows.
2. **Sync is one-way**, Mac → Windows. There is no merge, no pull of source back.
   Build outputs and logs come back the other way, into gitignored paths.
3. **Out-of-source builds only** (`build-msvc/` on Windows, as `build/` and
   `build-off/` are on the Mac). The source tree stays clean on both hosts.
4. **Nothing is claimed until it is measured.** A Windows capability is worth
   nothing until this box has answered it. Everything below marked *measured* was
   run on the target; anything marked *assumed* is a thing to probe, not to believe.
5. **Platform differences stay in the seams atui already uses** -- one header, one
   `.cpp` per platform, every variant listed unconditionally in the source list, a
   capability query that answers honestly, and link failure as the loudest answer.
   `PORTING.md` states this; a Windows port that scatters `#ifdef _WIN32` through
   shared code has failed even if it compiles.

## The machine (measured)

| | |
|---|---|
| Host | 10.196.88.64, Windows 10 Pro 10.0.19045 |
| Bitness | **32-bit** (`AddressWidth=32`, no `PROCESSOR_ARCHITEW6432`) |
| CPU / RAM | Intel i5-3470T, 2 logical CPUs, 3.5 GB |
| Disk | 81.5 GB free of 119.9 GB |
| Long paths | enabled (`LongPathsEnabled=1`) |
| VS2017 | Community 15.9.37902.15, `C:\Program Files\Microsoft Visual Studio\2017\Community` |
| MSVC | 14.16.27023 (`_MSC_VER 1916`, `_MSC_FULL_VER 191627054`) |
| Toolsets | v141, **v141_xp** (Win32 + x64) under VS2017 `VCTargets` |
| VS2019 | Community 16.11.37903.5, MSVC 14.29.30133 (v142), plus a v141 copy |
| VS2015 remnants | v140 and **v140_xp** in `C:\Program Files\MSBuild\Microsoft.Cpp\v4.0\v140` |
| SDKs | Windows Kits 10 (`10.0.10240.0`, `10.0.19041.0`) and Kits 8.1 |
| CMake | **3.31.12** at `C:\cmake\bin\cmake.exe` (no Ninja beside it, not on `PATH` -- invoke by full path) |
| Ninja | **1.13.2, 32-bit**, from `py -m pip install ninja` at `C:\Python\scripts\ninja.exe`. The copy VS2017 bundles is 64-bit and **cannot run on this OS** |
| Borland | `C:\BC5` = **Borland C++ 5.2**: `BCC.EXE` (16-bit), `BCC32.EXE`, `TLINK.EXE`, `TLINK32.EXE`, `ILINK32.EXE`, `MAKE.EXE` |
| Other | git 2.46.2, `tar.exe` (bsdtar), `py` = Python 3.14.10 (`python`/`python3` are Store stubs -- use `py`) |

The compile path was proven end to end: `vcvarsall x86` + `cl hello.cpp` produced
an exe that ran and returned its exit code. `__cplusplus` defaults to `199711L`,
which is the honest C++98 answer -- friendlier to the `gate98` rule than clang's
`gnu++14`.

Two consequences of 32 bits worth stating early. Only `Hostx86` compilers can run,
so **x86 is the working target**; `x86_amd64` preprocessed successfully, but no
x64 binary can run on this box, so x64 buys nothing here and is out of scope. And
32-bit Windows 10 still has NTVDM, which may make the Win16 target testable on
this same machine rather than needing real 3.x hardware -- an assumption to check
in the Borland phase, not a reason to plan around it.

## Transport: two channels, different jobs

Both are working and measured.

**SSH** (OpenSSH on the target, user `stash`) carries files and non-interactive
commands: `scp -r`, `sftp`, `tar` and direct `cmd`/`PowerShell` 5.1 execution.
Transfers are byte-identical -- SHA-256 verified in both directions, including a
CRLF file and a 2 MB binary -- at ~26 MB/s.

**The windows-mcp desktop** carries anything with a screen. `Screenshot`,
`Snapshot` with the UIA tree, and real mouse/keyboard input all work.

The division is not a preference, it is a limit: an SSH session on Windows has no
usable console for a TUI, so `atui.exe`'s interface cannot be exercised over SSH.
Build and self-test over SSH; **run and look at the TUI through windows-mcp**, by
launching a console window on the desktop and screenshotting it.

Two notes on the SSH setup. Password auth is driven by an `expect` wrapper today,
which is fine but clumsy -- `sftp -b` batch mode cannot use it at all, because
batch mode disables password prompts. Key auth would clean this up, but `stash` is
in the local Administrators group, so it means writing
`C:\ProgramData\ssh\administrators_authorized_keys`, which grants key-based admin
access from any machine holding the key. That is a deliberate decision for you to
make, not a convenience to take quietly.

## What Windows support already exists

More than expected, and none of it has ever been compiled.

- `src/net/sock_win.cpp` -- 227 lines, complete Winsock2 socket backend.
- `src/core/thread_win.cpp` -- `_beginthreadex` backend, matching the seam in
  `src/core/thread_platform.h`.
- Both are **already in the source list** -- `thread_win.cpp` unconditionally at
  `CMakeLists.txt:125`, `sock_win.cpp` at `CMakeLists.txt:144` inside the
  `ATUI_HAVE_NETWORK` block -- and `ws2_32` is already linked for `WIN32`
  (`CMakeLists.txt:212-219`), so they go live the moment `_WIN32` is defined. No
  source-list change is needed. The corollary is that a network-OFF build never
  compiles the Winsock backend, which is why P1 compiles without it and P3 is the
  phase that actually tests it.
- `_WIN32` branches already exist in ~15 shared files: `paths.cpp`, `journal.cpp`,
  `childenv.cpp`, `hostenv.cpp`, `promptfile.cpp`, `ini.cpp`, `permissions.cpp`,
  `main.cpp`, `sock.h`, `run_command_tool.cpp`, and in `tests/selftest.cpp`.
- tvision has a real Win32 console adapter (`tvision/source/platform/win32con.cpp`,
  selected at `platform.cpp:40`), and its `ncursesw not found` fatal error sits
  inside `if(NOT WIN32)`, so it is unreachable here.
- **Third-party dependencies beyond tvision: none.** No curl, no OpenSSL, no zlib.
  atui does plain HTTP over sockets and rejects `https` deliberately
  (`src/net/url.cpp:44-53`), so there is no TLS dependency to hunt for. The whole
  Windows link surface is tvision, `ws2_32` and the CRT.
- tvision builds no examples and no tests as a subproject, and it already carries
  the MSVC options atui would want (`/permissive- /Zc:__cplusplus`, `/MT` via
  `TV_USE_STATIC_RTL`).
- `ATUI_HAVE_THREADS` is already `1` on `_WIN32` (`src/core/thread.h:84-90`), and
  `appman_none.cpp` already has a `Sleep()` path, so the "no capability" backends
  already compile here.

## What is missing, or silently wrong

Ordered by how much it matters.

| What | Where | Why it matters |
|---|---|---|
| **No screen capture on Windows** | `screen_none.cpp:10` claims the slot | Needs a new `src/platform/screen_win.cpp` (`EnumWindows`/`GetWindowRect`/`GetWindowText`, GDI `BitBlt`). `screen.h:13` already anticipates hex HWNDs. CMake needs an `if(WIN32)` block setting `ATUI_HAVE_WIN_CAPTURE`; none exists -- only APPLE/UNIX branches do. |
| **A command's stderr was lost on Windows** -- fixed, W3 | `run_command_tool.cpp` | `_popen` captured nothing when the command redirected for itself, and `type nosuchfile` came back as `rc=1` with **empty output**: a failed command told the model it failed and never said why, while the message went to the console underneath the TUI. One pipe handed to both of the child's output handles fixes it, because merging is a handle question and not a shell-syntax one -- appending `2>&1` was worse than nothing (measured both; `TESTING.md`). |
| **`commandTimeoutIsEnforced()` was false on Windows** -- fixed, W3 | `run_command_tool.cpp` | The timeout was a message with no behaviour behind it, because `_popen` hands back nothing to hold on to. `CreateProcess` returns a handle and a job object is the process group, so the limit now stops the command *and* everything it started -- asserted by a test that was checked to fail without the job (`rc=-8`, a surviving `ping.exe`). |
| **Symlink containment does not exist on Windows** | `permissions.cpp:165-174` | `realpath` is used only where `pathAnchor() == "/"`, and `_WIN32`/`__OS2__` return the normalised form unchanged. A link inside the project pointing out of it is therefore inside it. This is the single cause of the three failing permission self-tests, and `PERMISSIONS.md:435-442` already documents it as a known limit. |
| **The decision log did not rotate on Windows** -- fixed, W2 | `permissions.cpp:1390` | The body of `rotateIfNeeded()` was inside `#if !defined(_WIN32)`, so `permissions.log` grew without bound and the 1 MiB / one-generation rule at `PERMISSIONS.md:416` silently did not happen. Not a platform limitation being worked around -- nobody had compiled it there. The remove-then-rename order the OS/2 comment explains was already the only order that works on Windows too; the guard was the whole bug. `permission: decision log rotates` now covers it, and was checked to cover it: the same test fails `rc=-3` against the previous file. |
| `clock.cpp` takes the non-reentrant path | `clock.cpp:9-11` | `gmtime_r`/`localtime_r` are gated on `__unix__`/`__APPLE__`; MSVC wants `_gmtime64_s`/`_localtime64_s`. Compiles, silently worse and thread-unsafe. |
| No console signal handling | `signals.cpp:7` | `_WIN32` excluded, correct for `sigaction`, but nothing replaces it. `Ctrl+C` kills the process mid-turn with no journal close. `SetConsoleCtrlHandler` is the answer, if we want one. |
| No `.rc`/manifest handling | `CMakeLists.txt` | OS/2 needed `tools/os2/atuipm.rc`; a Windows console app may want a manifest, and an XP build needs `SubSystem,5.01`. |
| `.gitignore` has no Windows build dir | `.gitignore:34-35` | `atui.exe` (L12) and `*.obj` (L8) are covered; `build-msvc/` is not. |

No `compat/` layer is needed for MSVC -- that tree exists because Open Watcom
ships no `<thread>`/`<mutex>`/`<chrono>`, which `src/net/sock.h:12-15` already
records as a fact about headers rather than about platforms.

## The cross-platform approach, settled

Everything above is a list of differences. This is the rule for handling them, so
the next platform -- Borland, Win16, whatever -- is a lookup rather than a
 rediscovery.

**1. One internal form; native only at the boundary.**
`core/paths.cpp` already gets this right and it is the pattern to keep: `isSep()`
accepts `/` and `\` unconditionally on every platform (`:62-64`), `pathAnchor()`
returns a canonical device part whatever was typed, `pathIsUnder()` compares
component-wise with case folding where the filesystem folds it, and
`normalizePath()` rebuilds with `/`. Both separators are *analysed* everywhere and
`/` is *emitted* internally, because DOS-derived APIs accept either. So no
platform branch is needed for separators, and none should be added. What is
allowed to be native is the moment a string is handed to an OS API or to a shell.

**2. A tolerant parser may accept bad input; it may not invent a different value.**
`json_args.cpp:82` is `default: out += e;` -- an unrecognised escape keeps the
character and throws the backslash away, so `C:\Users\x` arrives as `C:Usersx`.
That is not leniency, it is corruption: the wrong path, silently, on a tool that
creates files. Preserving both characters costs one statement and cannot affect
conformant input, because `\` followed by a non-escape character has no defined
meaning to preserve *against*. Do the same in both `json_extract.cpp` readers.

The rule has a limit worth writing down before anyone is surprised by it: `\t`,
`\n`, `\b`, `\f` and `\r` *are* JSON escapes, so an under-escaped path through
`C:\temp` is genuinely ambiguous and stays `C:` followed by a tab. No parser can
settle that one -- the sender has to double the backslash -- and guessing in that
direction would corrupt every legitimate tab in every edit the model makes. What
preserving buys is the unambiguous majority: `\U`, `\s`, `\o`, and the rest of the
letters that cannot escape.

**3. Fix what that investigation found on every platform, not just Windows.**
`case 'u'` existed in `json_args.cpp:62` and in **neither** `json_extract.cpp`
reader. So `"\u00e9"` in a model's answer decoded to the five characters `u00e9`
on Linux, macOS, OS/2 and Windows alike: any API that escapes non-ASCII -- and
several do -- was delivering mangled text, everywhere, today. That is a worse bug
than the one that led to looking, and it would have been missed by treating this
as a Windows issue. The three independent decoders are now one,
`core/jsonstr.{h,cpp}`, which also joins surrogate pairs rather than encoding each
half as three bytes -- an emoji reaching the terminal as two broken characters was
the same carelessness one step along.

One suspicion did not survive reading the transport, and it is recorded here
because the fix it was asking for would have been a rewrite. `parseToolCallFrags`
decodes `arguments` per chunk and `mergeToolFrags` concatenates the *decoded*
pieces, which looks like an escape split across two chunks surviving as a literal
backslash-and-`n`. It cannot survive: `SseStream::feed` does not emit a line until
it has one (`sse.cpp:16-28`), so `applyStreamDelta` is never handed half a payload
and every chunk's `arguments` literal is closed before it is decoded. There is no
boundary for an escape to straddle. The fact the decoder was relying on -- that its input is a whole SSE frame rather
than whatever the last read returned -- was promised by `sse.h` one level too
abstractly to be noticed: it said "one complete payload" and said nothing about
the JSON inside the payload, which is exactly what the caller was reasoning about.
The consequence is now spelled out where the promise is made.

**4. The path the policy decided on must be the path that gets opened.** -- done, W4
`normalizePath()` is a file static in `permissions.cpp:113` and the tools cannot
see it, so `edit_file` asks permission about one string and `fopen`s another
(`edit_file_tool.cpp:163,177,183,191`), including a journal entry built from a
third form (`core::absolutePath`, which does not collapse `..`). Move it into
`core/paths.{h,cpp}`, canonicalise once in the tool, and use that string for the
subject, the open and the journal. On Windows this also removes the drive-relative
and mixed-separator cases at the same time; on POSIX it closes the gap between
what `realpath` reasoned about and what gets written.

What was built is one notch different, and better for it: `normalizePath` stayed
where it is, and `tools::canonicalPath()` was exported from `permissions.h` as a
one-line wrapper over the same internal `absolutePath()` the decision uses. Moving
the canonicaliser would have given the tool a second function to keep in step with
the policy; a wrapper over the policy's own answer cannot drift. `edit_file` now
canonicalises once at the top of `execute()` and uses that for the read, the
writability check, the journal entry and the write, while its messages still quote
the path the model wrote (recognising its own words matters more than a second
spelling of the same file). `core::absolutePath` then receives an already absolute
path and returns it unchanged, so the journal names the file that was written
without it changing.

The case the fix is for is not exotic and was not hypothetical: `main.cpp:158,391`
chdir to the project root at start-up, so cwd and root coincide -- and the
divergence appears exactly when that does not happen (a failed `chdir`, a root set
after start-up). `tool: edit_file opens what it approved` makes them differ on
purpose, and was checked to fail (`rc=-3`, the edit landing in the working
directory's file) against the pre-W4 tool.

**5. Where the OS has a real primitive, use it instead of emulating POSIX.**
The measured case: `command + " 2>&1"` does not merge stderr under `cmd.exe`
(`TESTING.md`), and a failing command produces `rc=1` with no output, so the model
is told a command failed and never why. No spelling fixes that reliably -- it is a
handle-assignment problem, not a syntax problem. `CreateProcess` with
`STARTUPINFO.hStdError` set to the same write handle as `hStdOutput` makes the OS
merge them, and hands back a process handle, which is also what the deadline
(`commandTimeoutIsEnforced`) and killing a command's children have been waiting
for. One native spawn implementation fixes three recorded gaps. The same reasoning
applies to path resolution: `GetFinalPathNameByHandle` resolves both symlinks and
junctions where POSIX would have used `realpath`.

**6. A platform limit must read as a limit, not as a pass and not as a failure.**
The three permission self-tests fail on Windows for one cause: symlink resolution
is POSIX-only (`permissions.cpp:165-174`), and the fixture only creates the link
on non-Windows anyway (`selftest.cpp:1662`). The harness has `pass` and `fail` and
nothing else, so a capability a host cannot offer is indistinguishable from a bug.
It needs a third answer -- a reported *skip*, counted and printed in the summary --
and then those three tests say what is true: skipped here, because this host cannot
resolve a link and the policy says so. Silently `#if`-ing them out is the failure
mode that produced this whole situation: the suite has never been compiled on OS/2,
which is why none of this was found there.

**Order of work**, by how much it unblocks and how little it risks:

| | | |
|---|---|---|
| W1 | One JSON string decoder: `\u` everywhere, lossless unknown escapes | **done** -- `core/jsonstr.{h,cpp}`, both readers calling it; fixes wrong text on all platforms |
| W2 | Rotate the decision log on Windows (remove-then-rename, already the correct order) | **done** -- and the new test was checked against the previous file to be sure it fails there |
| W3 | `CreateProcess` spawn: shared std pipe, process handle, killable tree | **done** -- stderr captured, the limit enforced, the tree killed and the kill asserted; the deadline tests now run on Windows too, through harness helpers that know `sleep` from `ping` |
| W4 | Export the canonicaliser; tools decide and open the same path | **done** -- `tools::canonicalPath()`, `edit_file` opens that, and the test that makes cwd differ from the root was checked to fail without it |
| W5 | `GetFinalPathNameByHandle` resolution, plus skip-aware harness | restores symlink containment on Windows; makes the remaining failures honest |
| W6 | `PERMISSIONS.md`: shells, quoting, JSON escapes, and what Windows cannot promise | the policy has to say what the code now does |

What this deliberately does **not** include: per-file `#ifdef _WIN32` sprays
(the seams already exist and are used correctly), making POSIX do something worse
to match Windows, or teaching the tests to prefer `\` -- the internal form stays
`/` and the tests keep writing it.

## Build decisions to settle before the first configure

1. **Generator: Ninja**, settled -- but only after measuring which Ninja. VS2017's
   bundled copy is a 64-bit executable and this is a 32-bit Windows, so it cannot
   execute and CMake calls that "unknown error"; the 32-bit one from
   `py -m pip install ninja` works, and `configure.bat` runs the candidate before
   trusting it and falls back to the MSBuild generator otherwise.
2. **CMake version -- settled, and no longer a blocker.** atui asks for 3.16 and
   tvision declares `3.5...3.30`; the installed 3.31.12 satisfies both, so no policy
   override is needed. Worth remembering for later: 3.31 is the last of the 3.x
   line, and CMake 4.x drops compatibility with `cmake_minimum_required` below 3.5,
   so a future move to 4.x wants tvision's floor raised in our fork -- a one-line
   change, but not one to discover mid-build.
3. **Runtime library: `/MT` static** for release, matching how the OS/2 build
   ships one self-contained binary and avoiding the VC redist on a target machine.
   Whatever is chosen, tvision and atui must agree -- MSVC will not let them mix.
4. **One `_WIN32_WINNT` everywhere.** tvision sets `0x0600` privately while the
   19041 SDK defaults the rest of the tree to Win10, which invites mismatched
   inline headers. Decide a single value (0x0601 is a reasonable floor for the
   VS2017/Win10 phase) and define it for every TU.
5. **`UNICODE` stays undefined** unless tvision proves it needs it -- atui passes
   `char` paths around, and turning on `TCHAR` semantics halfway through is a bad
   way to find out.
6. **Language level stays C++98 in `src/`.** MSVC 19.16 will happily accept far
   more than Watcom does, so the compiler cannot be the gate. `gate98.py` runs on
   the Mac with clang and has to stay in the loop -- run it before every sync, not
   after a Windows build. Note it reports two known `tvision/tstrview.h` lies;
   those are pre-existing, not Windows damage.
7. **Work dirs**: source at `C:\Users\stash\atui`, build at
   `C:\Users\stash\atui-build` (outside the source tree entirely, so nothing
   Windows produces can look like a source file).

## The sync loop

`tvision` is a submodule (our fork, `stash763/tvision`, currently `e18590e`) and is
**not yet on the Windows box** -- the first tree copy skipped it. The whole build
needs it, so the copy has to include it.

Until key auth is settled, sync is: build a tarball, `scp` it, extract. That is
measured working. The `COPYFILE_DISABLE=1` prefix is not decoration -- without it
macOS ships extended attributes that Windows `tar` materialises as 229 `._*` files
in the tree, which is what happened on the first copy.

```sh
# Mac side: source + submodule, no build junk
cd /Users/stash/code/atui
COPYFILE_DISABLE=1 tar czf /tmp/atui-win.tgz \
  --exclude='build*' --exclude='.git' --exclude='*.log' --exclude='.atui' \
  src tests tools compat tvision CMakeLists.txt PORTING.md TESTING.md PERMISSIONS.md
```

A tracked-only variant, which is the honest default because it can only ever ship
what is committed, uses `git archive` twice (once per repo, since submodules are
separate archives).

The Windows side is `tools/win/`, written on the Mac and synced like any other
source, following `tools/os2/build.cmd` in putting the machine's paths in a script
that says why they are what they are:

| script | what it does |
|---|---|
| `env.bat` | runs `vcvarsall x86` and puts CMake, Ninja and the tree's location in the environment. Called by the others; usable alone to get a shell that can build. Deliberately no `setlocal` -- see the note in it. |
| `configure.bat` `[--release] [--net] [--clean]` | configures `"%USERPROFILE%\atui-build"`. Network off unless `--net`. Probes Ninja, falls back to MSBuild. |
| `build.bat` `[-j N] [--keep] [--release]` | builds. `--keep` passes `-k 0` to Ninja, which turns a first pass over a new compiler from twenty round trips into one. |
| `selftest.bat` | runs `atui.exe --self-test`, looking in both the single- and multi-config places for it, and prints the binary's own build timestamp before the results. That line matters: a failed build leaves the previous `atui.exe` in place, and the suite will report a clean pass about code that does not compile. It happened -- see the W3 entry in `TESTING.md`. |

The Mac-side driver that ties sync to these does not exist yet. The plan is still
for it to be `tools/atui-win`, following `tools/atui-remote` -- which already does
sync → invoke → pull logs for OS/2 and is the pattern this project uses -- offering
`sync`, `build`, `test`, `shell`, `pull-logs`, and a Windows-specific `run-gui`
that goes through windows-mcp rather than SSH, precisely because of the console
limitation above.

## Phases

**P0 -- Get a configure to happen.** Sync the tree with tvision in it. Configure
`C:\Users\stash\atui-build` with Ninja, `Win32`, VS2017, `-DATUI_HAVE_NETWORK=OFF`
first (fewer moving parts, and `build-off/` shows the no-network configuration is
already a supported shape). *Exit: configure completes with no CMake errors.*
**Done** -- configures clean. What it took: a 32-bit Ninja, and handing CMake its
path explicitly, because searching PATH itself finds the 64-bit one it cannot run.

**P1 -- Compile.** Build tvision, then atui's objects, with `/W3`. Expect a list of
genuine errors -- this code has never seen MSVC. Fix them on the Mac, resync,
repeat. Keep the error list in `TESTING.md` as it goes; the list is the finding.
*Exit: `atui.exe` links for `Win32`.* **Done** -- links, 1.92 MB Debug, no
warnings from our sources. The list was four things, all small, all recorded in
`TESTING.md`; the suite needed the most of them, for the reason in the paragraph
above about per-suite platform branches.

**P2 -- Run headless.** `atui.exe --self-test` over SSH, both configurations
(`ATUI_HAVE_NETWORK` off then on). This is the highest-value signal available over
SSH because it needs no console: 18 test files, `tests/selftest.cpp`, compiled into
the binary, so it tests statics too. *Exit: self-test passes, or every failure is
understood and recorded rather than skipped.* **In progress -- 140 passed, 4
failed** on the first run, from 137/7 once two test-portability bugs were fixed
(`/tmp` assumed; paths concatenated into JSON without escaping backslashes). The
four left are `run_command`'s stderr merging under `cmd.exe` and the permission
model's Windows holes -- both P5, both understood. Detail in `TESTING.md`, which
also carries a parser-lenience question this surfaced that needs a decision.

**P3 -- Network.** Turn `ATUI_HAVE_NETWORK` on, which activates `sock_win.cpp`, and
test against `tools/fake_stream.py` running **on the Mac** as an OpenAI-compatible
server that the Windows build talks to over the LAN. That exercises the real
Winsock path -- including the Winsock-specific behaviours `sock.h:19-37` warns
about: no `SIGPIPE`, `close()` on a socket handle, `errno` not maintained,
non-blocking `connect()` returning -1 while succeeding -- without needing a model.
*Exit: a chat turn completes against the fake server; the Winsock quirks are
confirmed or corrected in `sock.h`.*

**P4 -- Look at it.** Launch `atui.exe` in a console window on the desktop via
windows-mcp and drive it with `Screenshot`/`Snapshot`/`Type`. This is the phase
where UTF-8 output, the code page, and keyboard input are actually verified.
*Exit: the TUI renders, keys land, a session is usable on screen.*

**P5 -- Fill the Windows gaps.** `screen_win.cpp`, the spawn deadline, `clock.cpp`,
console CP, signal handling, and the permission-policy question above -- each behind
its capability query, each measured before it is claimed.

**P6 -- Then the older Windows targets.** In rough order of how much they can
reuse P1's work: v141_xp and v140_xp (both present; note the last SDK carrying XP
libraries was 10.0.17763 and this box has 10.0.10240/19041 plus 8.1, so whether XP
targeting actually links is an open question to probe), then Borland C++ 5.2. BC5
has both `BCC32.EXE` and 16-bit `BCC.EXE`/`TLINK.EXE`, so Win32s-era and Win16 are
one toolchain apart. Each gets a probe harness in `tools/` first, the way
`tools/os2/probe_*.cpp` did for OS/2: measure what the compiler actually does
before restructuring anything to suit it. The OS/2 build is the template for this
shape of work -- `tools/os2/build.cmd` is 926 lines of learned facts, and it did
not get written in one go.

## Open questions

1. Generator, runtime library, `_WIN32_WINNT` floor -- the three decisions above
   that are easier to make once than to undo later.
2. Is x86-only acceptable for all Windows targets? This box cannot run x64, and
   32-bit is what the legacy targets want anyway.
3. Key-based SSH, yes or no? It unlocks `sftp -b`, `rsync`, and uncluttered
   scripting; the cost is passwordless admin on the box.
4. How far back should the VS2017 build itself run -- Windows 10 only, or should
   the phase-1 binary also work on 7/8.1? It changes `_WIN32_WINNT` and nothing else.
5. Which shell does atui treat as *the* Windows shell for `run_command` --
   `cmd.exe` for reach, or `powershell` for behaviour? The permission model needs
   an answer before it can be made as strict on Windows as it is on POSIX.
6. Should `--self-test` be extended to assert the Windows-specific behaviours we
   measure, so a regression on any host shows up as a test failure rather than a
   remembered fact?
