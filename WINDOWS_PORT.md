# Windows Port Plan

How atui gets built and tested on Windows, and what it takes to keep the other
targets whole while doing it. This is the plan; `PORTING.md` is the contract the
code has to keep, and `TESTING.md` is where the measurements go.

Scope, in order: **VS2017 / Win32 / Windows 10 first**, then the older Windows
targets. Nothing here changes what the OS/2, DOS, Linux or FreeBSD builds do.

Status: P0-P3 done on the target, W1-W8 finished, W9 tried on the target and withdrawn
(it had nothing to catch -- see the gap table), and **P4 -- the TUI on a real Windows
screen -- measured**. That phase found one real defect in atui rather than in Windows,
display cells counted as bytes, and it is now fixed (`W8`): the transcript, the tables and
the input line are measured in columns on every platform that does not fold. atui.exe
builds and links for Win32 with the network compiled in, and on the current target the
self-test is **216 passed, 0 failed, 2 skipped** headless and **220 / 0 / 2** against the
live model on the LAN. Neither
skip is a surprise: one is the missing console control handler (`P5`), the other is
symbolic-link coverage this project declines on Windows. The measurements are in
`TESTING.md`; the target machine changed on 24 Nov 2026 and the table below is its facts,
re-measured.

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
6. **Text put behind a `#if` is text every platform sends.** The host-brief commit split
   one tool description into three arms so each could name its own shell, lost a closing
   brace in the process, and from that commit forward every build on every platform sent
   an invalid chat request -- which no test caught, because every test that looked at the
   request searched it and none parsed it. Two things follow, and both are now enforced.
   **A document atui builds is a document atui parses**: the schema and the request body
   go through a strict parser that shares no code with the thing under test (`W7`). And a
   change made for one platform is measured on the others, since the `#if` is what makes
   an edit local and a string literal is what makes it global.
7. **A measurement is only as honest as the instrument that made it.** P4 found four
   defects in its own tools before it found anything about Windows, and each one was
   capable of producing a plausible result about the subject: printing non-ASCII through a
   console code page turns an em dash into `-` and an accented letter into `?`; setting an
   output encoding inside a reader changes the console of the program being read; a failed
   type cast in the reader left it reading row 0 forty-three times and printing forty-three
   rows of "data"; and once a process gives up its console, its warnings have nowhere to
   go, so the reason for a failure is replaced by a handle error. So: **print what was
   read**, in a form no code page can improve on; **never mutate the subject** to make the
   reading easier; and prefer an instrument that fails loudly to one that returns
   something. The corollary applies to fixes as much as to tools -- a change built on a
   likely-looking diagnosis that was never tested against an alternative gets taken back
   out, which is what happened to the buffer share mode this month.

## Scope: the display language is English

Decided 25 Nov 2026, after `P4` measured what a Windows console can and cannot show.
**atui supports English.** Rendering Chinese, Japanese or Korean well is a non-goal, and
with it goes the East Asian console mode that any CJK display on this platform would have
needed: detecting code page 932/936/949/950, laying ideographs out at the *one* cell per
character that console was measured to advance (not the two CP 437 advances), folding line
drawing to ASCII because an East Asian console misdecodes CP 437 box glyphs, and tuning
`W8`'s table per console on top. Three measurements are why this is a decision rather than
an oversight: the glyphs are not in the font, so no code atui writes can supply them; an
East Asian console that does have them breaks atui's line drawing; and it advances an
ideograph differently again. Writing that mode here would mean choosing console behaviour
from a box that shows only boxes.

Four things the decision does **not** relax, because none of them is about displaying
another language:

- **UTF-8 stays the internal form.** An English product still meets accented names,
  foreign place names and an em dash in nearly every paragraph a model writes.
- **The boundary stays correct.** `W8`'s cell measure and the fold are what make `café`,
  `—` and `╭──╯` line up. Neither is CJK-specific and neither is being walked back.
- **Nothing stored is folded.** Every fold call site is in the draw path in
  `src/tui/app.cpp`; the journal and session writers keep the bytes the model sent, so an
  answer in an undisplayable language survives and can be copied out of the session file.
- **Text atui cannot render is still measured correctly.** A Han ideograph costs two cells
  in `core/cells.h` even where its glyph is a box, because the console advances it two and
  everything after it on that line has to know.

Two known limits are accepted under this scope rather than queued: the Indic combining
marks absent from `core/cells.h`, which over-count by one cell and so wrap a row early --
the safe direction -- and the `?` that stands in for a character the terminal cannot spell,
which reads as what it is and does not pretend to be a transcription.

## The machine (measured)

**The target changed on 24 Nov 2026.** The old box (`10.196.88.64`) was replaced by
`10.196.88.95`, hostname `WIN10TEST32B`. It was not abandoned in frustration: the old
machine vanished from the network twice, the first time ending in a *logged, planned*
Windows-Update restart (`svchost … Service pack (Planned)`, exit `0x80020010`, no
`Kernel-Power` 41 and no bugcheck), the second time with **no reboot, no sleep and no
crash event at all** -- same `LastBootUpTime` before and after -- which is a hypervisor
pausing the guest rather than Windows doing anything. Nothing in either episode involved
atui: it was idle at its prompt, and the capture endpoint had no request in flight. The
history of that machine is still useful context, so it is kept below the table and
marked as such. **Everything in the table was re-measured on the new box**, and the
Windows tree was rebuilt there from a commit rather than copied (see *The sync loop*).

| | |
|---|---|
| Host | `10.196.88.95`, `WIN10TEST32B`, Windows 10 Pro 10.0.19045 |
| Bitness | **32-bit** (`X86-based PC`, no `PROCESSOR_ARCHITEW6432`) |
| CPU / RAM | 2 logical CPUs, 3.9 GB |
| Disk | 86.6 GB free on C: |
| Display | 1366x768 at 96 dpi, one display -- P4's window sizes follow from this |
| Long paths | enabled (`LongPathsEnabled=1`) |
| Code pages | OEM 437, ANSI 1252; a new console starts at 437 |
| Console | conhost (no default-terminal delegation set), TrueType face by default; `C:\Windows\Fonts\simsun.ttc` exists but is not the console face |
| PowerShell | 5.1.19041.2673, `FullLanguage`, session 1 (the interactive one) |
| VS2017 | Community, `C:\Program Files\Microsoft Visual Studio\2017\Community` |
| MSVC | 14.16.27023 -- the same compiler the old box used |
| CMake | **3.31.12** at `C:\cmake\bin\cmake.exe` |
| Ninja | **1.13.2 win32** from `py -m pip install ninja`, landing at `C:\Python\Scripts\ninja.exe`, which is where `tools/win/env.bat` already looks. The wheel is `py3-none-win32` and runs; VS2017's bundled Ninja is 64-bit and could not |
| Borland | `C:\BC5` exists; its contents are **not re-measured on this box** |
| Other | git, `tar.exe` (bsdtar), `py` = Python 3.14.10 |
| Not re-measured | `v141_xp`, VS2019, the SDK list, the BC5 tool list, `__cplusplus` defaults. These were facts about the old box; they are checked when `P6` needs them, not carried over as assumptions |

The compile path was proven end to end on the old box: `vcvarsall x86` + `cl hello.cpp`
produced an exe that ran and returned its exit code, and `__cplusplus` defaulted to
`199711L` -- the honest C++98 answer, friendlier to the `gate98` rule than clang's
`gnu++14`. On this box the equivalent proof is the whole tree building and its own
self-test passing, which is a stronger test than `hello.cpp` and was done instead.

One more consequence of the box showed up that is about the shell rather than the
compiler: **Windows PowerShell 5.1 has no `[short]` type accelerator** -- `[int16]`
works. Spelled wrong it does not fail loudly: the assignment throws, the field keeps
what it had, and `tools/win/console_buffer.ps1` went on to read row 0 forty-three times
and print it as forty-three rows of the measurement. A script that measures something
has to print what it read, not just that it read.

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
| **A UTF-8 file name is not the same file** | `src/core/paths.cpp`, the A-family file calls | `_access`, `CreateFileA` and `FindFirstFileA` read the bytes they are handed as the **ANSI** code page, not UTF-8. Measured on the target: a file whose real name is `caf\u00e9.txt` is *not found* by atui's UTF-8 bytes; a Japanese name comes back from a listing as `p3_??.txt`, irrecoverably; and creating a file with UTF-8 bytes **succeeds** (`err=0`) by making `caf\u00c3\u00a9.txt` -- a file nobody asked for, with no complaint. So the policy decides about one name and the filesystem receives another, and a lossy mapping can also collapse two names onto one file, which is the 8.3-alias problem from `W5` through a wider door. The fix is the wide API behind the seam in `paths.cpp`, with the boundary rule from `PORTING.md`: refuse a name that cannot survive the round trip rather than accepting a near-miss. Nothing about DOS, OS/2 or the Win16/Win32s builds changes to accommodate it. |
| **A string's display width was its byte count** -- fixed, `W8` | `src/core/cells.{h,cpp}`, nine call sites | The wrapping rule said so out loud: *"One byte is one display column (the app never measured cells)"*. That holds on an 8-bit screen, and OS/2's fold to ASCII hides it anyway; on a Unicode console it does not. Measured on the target with a probe whose rows line up only if widths are charged right: prose drew `A—B` with two blank cells after the dash, `é` gained one, arrows and curly quotes two, and an ideograph was charged three columns against the two the console draws it in -- while the very same characters looked right inside a fence, because a short fenced line is never padded and so is never measured. Fixing the wrapper would have moved the drift rather than removing it: nine places measured a row their own way, and two were worse than ragged -- markdown's long-word split and the input line's `rewrap()` both cut a character in half at a byte boundary, so half a character reached the screen and anything exported from the transcript, and the transcript's draw cursor advanced by bytes, which drew every span after non-ASCII too far right. Now one measure, `core/cells.h`, answers for the whole app: table-driven, so a headless self-test can check it, and erring towards more columns so the failure mode is a row that wraps early rather than text drawn over the column it does not own. Nothing about OS/2 or DOS changes, because folding leaves them where byte equals cell anyway. Nine tests, each seen failing first -- and one of them was vacuous the first time (it compared each table row against itself), so it was only believed after the byte count was put back and it failed again. |
| **Console encoding: measured on the box, and the console is not the suspect** | `tvision/source/platform/win32con.cpp` | The Win32 adapter converts UTF-8 to UTF-16 and writes with `WriteConsoleOutputW`, reads with `ReadConsoleInputW`, and sets the console's code pages. Watched happening on Windows 10 19045 with conhost: an em dash, Latin-1 (`café naïve über Señor São`), box drawing (`╭─╮ ┌─┬─┐ ├─┤ ╰──╯ ─│─`), shades (`░▒▓█`), arrows and curly quotes all render as themselves, and typed non-ASCII reaches the request body byte-exactly (`U+00E9`, `U+2014`, `U+65E5` in the captured JSON). **CJK draws as empty boxes, and that is the font, not atui**: the same code points written by PowerShell into the same console produced the same empty boxes and the same cursor advance, which is the control experiment that makes the accusation unnecessary. The console's own arithmetic was measured too -- `x日本y` advances **6 cells**, `x—y` advances **3**, `xcéf y` advances **5** -- which is what `W8` has to match. Legacy raster-font console is still unmeasured, and measuring it is declined: it means
changing the console font in the box's own settings, which is not this project's to change. |
| **The TUI's screen cannot be read by another process** | `tvision/source/platform/conctl.cpp:251` | atui draws on a screen buffer it created and made active. Measured across eight console operations (`tools/win/attach_probe.c`): a program that calls `SetConsoleActiveScreenBuffer` cannot have its console read by any other process -- `AttachConsole` fails with `ERROR_SHARING_VIOLATION`, and **the buffer's share mode makes no difference**, which is worth recording because the first fix tried was changing that share mode, on a plausible story, and measured no difference at all. So no external tool, and no screen reader, can read what atui drew. It makes `P5`'s capture slot load-bearing rather than nice: on Windows, measuring the TUI means asking atui itself. |
| **No screen capture on Windows** | `screen_none.cpp:10` claims the slot | Needs a new `src/platform/screen_win.cpp` (`EnumWindows`/`GetWindowRect`/`GetWindowText`, GDI `BitBlt`). `screen.h:13` already anticipates hex HWNDs. CMake needs an `if(WIN32)` block setting `ATUI_HAVE_WIN_CAPTURE`; none exists -- only APPLE/UNIX branches do. |
| **A command's stderr was lost on Windows** -- fixed, W3 | `run_command_tool.cpp` | `_popen` captured nothing when the command redirected for itself, and `type nosuchfile` came back as `rc=1` with **empty output**: a failed command told the model it failed and never said why, while the message went to the console underneath the TUI. One pipe handed to both of the child's output handles fixes it, because merging is a handle question and not a shell-syntax one -- appending `2>&1` was worse than nothing (measured both; `TESTING.md`). |
| **`commandTimeoutIsEnforced()` was false on Windows** -- fixed, W3 | `run_command_tool.cpp` | The timeout was a message with no behaviour behind it, because `_popen` hands back nothing to hold on to. `CreateProcess` returns a handle and a job object is the process group, so the limit now stops the command *and* everything it started -- asserted by a test that was checked to fail without the job (`rc=-8`, a surviving `ping.exe`). |
| **Symlink containment did not exist on Windows** -- fixed, W5 | `permissions.cpp` | `realpath` was used only where `pathAnchor() == "/"`, so every drive-anchored path came back unresolved and a link inside the project pointing out of it counted as inside. Windows asks a different question -- open the path and ask the handle what it turned out to be -- which follows the whole class of reparse point, junction included, and a junction needs no privilege to create (`mklink /J`), which is what made this a hole rather than a limitation. Measured; see `TESTING.md`. |
| **The decision log did not rotate on Windows** -- fixed, W2 | `permissions.cpp:1390` | The body of `rotateIfNeeded()` was inside `#if !defined(_WIN32)`, so `permissions.log` grew without bound and the 1 MiB / one-generation rule at `PERMISSIONS.md:416` silently did not happen. Not a platform limitation being worked around -- nobody had compiled it there. The remove-then-rename order the OS/2 comment explains was already the only order that works on Windows too; the guard was the whole bug. `permission: decision log rotates` now covers it, and was checked to cover it: the same test fails `rc=-3` against the previous file. |
| `clock.cpp` takes the non-reentrant path | `clock.cpp:9-11` | `gmtime_r`/`localtime_r` are gated on `__unix__`/`__APPLE__`; MSVC wants `_gmtime64_s`/`_localtime64_s`. Compiles, silently worse and thread-unsafe. |
| **Console control events: installed, measured, withdrawn** | `src/core/signals.{h,cpp}` | `signals.cpp` excluded `_WIN32` with nothing replacing it, so the story looked like a missing `SetConsoleCtrlHandler`. One was installed on 25 Nov 2026 and taken back out the same day, because on this platform there is nothing for it to catch. Ctrl+C does not arrive: tvision clears `ENABLE_PROCESSED_INPUT` so Ctrl+C comes through as the key `0x03`, measured with the tick traced -- no handler call, no `^C` echo, no stop, ticks still at 20 Hz. And it must not be read as a quit even where it does arrive, because **Ctrl+C is the copy key on Windows, OS/2 and DOS**; atui exits through its menu. The events that do arrive -- close, logoff, shutdown -- end the process when the handler returns, so a flag set there is never read (measured: closing the console window killed a traced atui with no stop line while ticks ran). Ctrl+Break is the one event that would work, and it is not implemented because nothing here can generate it: keyboard injection cannot synthesise Break, and no other process can attach to atui's console to ask (error 32, at any share mode). The suite says so out loud rather than being quietly smaller: POSIX registers four `signal:` tests, Windows and OS/2/DOS register one that skips and names its own reason. |
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
| W5 | `GetFinalPathNameByHandle` resolution, plus skip-aware harness | **done** -- reparse points resolved on Windows, Windows suite green at 148/0, and a host that cannot make a link now reports a counted skip instead of a silent `#ifdef` |
| W6 | `PERMISSIONS.md`: shells, quoting, JSON escapes, and what Windows cannot promise | **done** -- and one sentence in it had to be reversed after measuring (`/s` is not what keeps a quoted command whole; the quotes are) |
| W7 | Requests are documents: build one, then parse it | **done** -- the fault the port found in atui rather than in Windows. A `#if` added for the run_command description cost a brace, and since then every request offering a tool was invalid JSON on every platform. Found by a server's 500, misdiagnosed as the server's fault, then caught by asking the bytes. Now: `tests/json_valid.h` (a strict validator sharing nothing with the product), the schemas and the wire body checked by it, and `note()` in the harness so a failure says what it was |
| W8 | A string's display width is not its byte count | **done** -- found by `P4`, and it was not a Windows bug: `wrap.h:4` said the app never measured cells, and nine places measured a row their own way. One measure in `core/cells.h` now answers for all of them (wrapper, markdown's word split and inline column counter, table cell widths *and* their padding, `Line::width`, the title cut, the draw cursor, the input line's wrap/clip/padding/caret). Verified on the screen both ways: with the fix the transcript is tight and fills its width; with the byte count put back on one line the drift returns exactly where it was. Nine tests, and the table test had to be rewritten after it was caught comparing each row against itself |
| W9 | Ask a Windows console to stop (`SetConsoleCtrlHandler`) | **withdrawn the same day** -- installed, then measured against the screen it was for and found to have nothing to catch: Ctrl+C reaches tvision as the key `0x03` rather than as an event (no handler call under a traced tick), Ctrl+C is the copy key on this platform so it must not quit anything, and the events that do arrive end the process before the flag can be read (window closed: died with no stop line). Ctrl+Break would work and cannot be generated here. `core/signals.h` keeps the measurements; the skip in the suite names the reason |

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

Until key auth is settled, sync is: build an archive, `scp` it, extract. The recipe
below is the one in use, and it ships **the commit** rather than the working tree:

```sh
# Mac side: exactly what is committed, superproject and submodule, into atui/
cd /Users/stash/code/atui
git archive --format=tar.gz --prefix=atui/ HEAD          > /tmp/atui-src.tar.gz
git -C tvision archive --format=tar.gz --prefix=atui/tvision/ HEAD > /tmp/atui-tv.tar.gz
# Windows side: extract both into %USERPROFILE%, which creates atui\ and atui\tvision\
```

Two rules, both bought by making the mistake. **Ship the commit, not the tree**: a
`tar` of the working directory was 273 MB and carried `build-off/`, `build-asan/`,
`build-nothreads/`, a dozen `build-*.log` files and a `.DS_Store`, all of them
gitignored and none of them source -- and it also smuggled a file named `-`, which was
gitignored *nothing* and turned out to be an artifact of a mistyped redirect that
`git add -A` had committed. `git archive` cannot ship any of that, and as a side effect
it proves the commit is self-sufficient: if the Windows build needs a file that is not
committed, it fails and says so. **Archive with `--prefix=atui/`**: without a prefix the
members are rooted at `./`, so extracting in the profile scatters the whole tree loose
there instead of into `atui\`, which happened and had to be cleaned up file by file
(the removal was driven off the archive's own entry list, never off a hand-typed one).

`COPYFILE_DISABLE=1` still matters for any `tar` that is not `git archive`: without it
macOS ships extended attributes that Windows `tar` materialises as `._*` files in the
tree, which is what happened on the very first copy.

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
understood and recorded rather than skipped.* **Met, and then some -- 148 passed,
0 failed** since `W5`; at the point it was first met it was 144/3 with the three
understood, which is what the criterion asked for. (That was the no-network
configuration; the same suite with `--net` is 204 passed and 1 skipped, which is P3, and
the binary now prints `self-test build: network=on|off` on the first line because a
number that changes with the configuration is not evidence until the configuration is
beside it.) This
phase paid for itself: it is the first time the suite has ever been compiled on a
non-POSIX host, and it found bugs that were not Windows cosmetic issues -- a `\u`
decoder missing from both network readers (wrong text on every platform), a
decision log that never rotated, and a policy that decided about one file while the
tool opened another. The first run was 137/7; two of those were test-portability
bugs (`/tmp` assumed; paths concatenated into JSON without escaping backslashes) and
the rest are the gap table above. Detail in `TESTING.md`.

**P3 -- Network.** Turn `ATUI_HAVE_NETWORK` on, which activates `sock_win.cpp`, and
test against `tools/fake_stream.py` running **on the Mac** as an OpenAI-compatible
server that the Windows build talks to over the LAN. That exercises the real
Winsock path -- including the Winsock-specific behaviours `sock.h:19-37` warns
about: no `SIGPIPE`, `close()` on a socket handle, `errno` not maintained,
non-blocking `connect()` returning -1 while succeeding -- without needing a model.
*Exit: a chat turn completes against the fake server; the Winsock quirks are
confirmed or corrected in `sock.h`.*

**Done**, and the file that was supposed to be the risk turned out to be the least
interesting part of it: `sock_win.cpp` compiled clean first time. Two things worth more
than that came out of the phase.

The first is that **the test harness, not the product, was the POSIX-only part.**
`tests/http_pipe.h` is the scripted responder every transport test talks to, and it
included `<arpa/inet.h>` and `<poll.h>` -- so the transport had never been *tested* on
Windows either, which no failing build had mentioned. It goes through one seam now,
`tests/pipe_sock.h`, and that seam deliberately does not reuse `src/net/sock.h`: a
harness that shares its subject's code cannot show the subject wrong, and the green
result would have been two copies of one misunderstanding. With the seam in, the first
Windows run of the transport was 202/2 and both failures were commands spelled for the
other shell -- `touch 'path'`, where cmd treats a single quote as a character and says
the syntax is incorrect -- now spelled through `make_file_command()` and
`in_dir_command()` after measuring what cmd does with each quoting and separator
(`type nul > "path"` creates the file with either separator, measured).

The second is the exit criterion met one step beyond loopback: `tools/fake_stream.py`
on the Mac -- with `--listen`, because a streaming endpoint should not open itself on a
LAN by default -- and `live: model list` and `live: chat stream` passing from Windows
across a real network to it. The other two live tests fail against that server exactly
as they would anywhere, since it has no model behind it: no reasoning deltas, no tool
call. Recorded, not hidden.

And one item in `sock.h`'s Winsock list, which had been inferred from the API, was
**wrong in the direction of danger**: the C library's `close()` on a socket handle does
not return -1 and leak, the way it does on OS/2. The UCRT treats a descriptor it does
not own as an invalid parameter and fails the process fast, 0xC0000409 -- the same code
a stack overrun reports, which is how the mistake would be diagnosed. Numbers and the
rest of it in `TESTING.md`.

**P4 -- Look at it.** Launch `atui.exe` in a console window on the desktop via
windows-mcp and drive it with `Screenshot`/`Snapshot`/`Type`. This is the phase
where UTF-8 output, the code page, and keyboard input are actually verified. What
"verified" means now that `PORTING.md` states the rule: a model answer containing an em
dash, `caf\u00e9`, a CJK character and a box-drawing run reaches the screen unchanged in
a modern console; the same answer in a legacy console with a bitmap font degrades
predictably rather than into noise; typing a non-ASCII path survives `ReadConsoleInputW`
and reaches the policy as the same bytes; and `ATUI_FOLD=1` on the same binary produces
the ASCII transcript, which is the check that the fold is a terminal property and not an
OS one.
*Exit: the TUI renders, keys land, a session is usable on screen, and the four characters
above are accounted for.*

**Measured on the desktop, 24-25 Nov 2026 (exit met).** On `WIN10TEST32B`, 1366x768,
conhost with its default TrueType face, driven through windows-mcp against scripted
endpoints on the Mac:

| criterion | what was watched | result |
|---|---|---|
| renders | menu (`File Sessions Switch model About`), header, transcript, input line, status bar showing provider and model read from `ATUI_CONFIG` | **yes**, first time on this box |
| keys land, session usable | `--ask` prompt sent, answer streamed and laid out, input line accepted typed text, `Alt-X` quit | **yes**; exit returned `EXITCODE=0` with the console's own palette and buffer restored |
| em dash | prose and fenced | **reaches the screen**; at the time it was charged 1 cell in a fence and 3 in prose, which is what led to `W8` (now fixed -- prose is tight and the paragraph fills its width) |
| `café` | Latin-1 in prose, and typed | **renders as accented letters**; typed form verified in the request body |
| CJK | written by atui, and by PowerShell as a control | **the font, not atui**: both drew empty boxes, both advanced 2 cells per ideograph. Displaying CJK is out of scope -- see *Scope: the display language is English* |
| box drawing, shades, arrows, curly quotes | `╭─╮ ┌─┬─┐ ├─┤ ╰──╯ ░▒▓█ → ← “ ” …` | **all render** as themselves |
| typing non-ASCII | `Type` sent `café — 日 ok` into the input line; request captured by `tools/dump_request.py` | **byte-exact** in the JSON body: `U+00E9 U+2014 U+65E5` |
| `ATUI_FOLD=1` | same binary, same fixture | **the ASCII transcript**: `-`, `cafe`, `->`, `+--+`, `????` for CJK -- and no gaps, folded or not, because folding leaves nothing multi-byte to measure |
| resize | window dragged to ~520x260 while streaming | **reflowed**: text rewrapped, menu intact, status bar dropped its hint text instead of garbling it |

Two things this phase needed that did not exist and now do. `tools/win/console_buffer.ps1`
reads the screen buffer of a running console program as the UTF-16 units it holds, with
`-CodePoints` to spell non-ASCII out so no code page can improve on it, and `-Out` to
write bytes; it cannot read atui's screen, and says so with the reason (see the gap
table). `tools/win/attach_probe.c` is the eight-case measurement behind that claim.
And `tools/dump_request.py` grew `--listen`, because the capture server has to be
reachable from the machine being measured.

Two lessons from instruments rather than from atui, both recorded where they bit. A tool
that prints through the console's code page **lies about non-ASCII**: the first version of
the buffer reader reported an em dash as `-` and `é` as `?`, because CP437 was in the way,
and it looked like a finding about the buffer. And a tool must not change what it measures:
setting `[Console]::OutputEncoding` inside the reader changed the *target's* console code
page from 437 to 65001, and the next reading reported 65001 as though that had been the
discovery.

Not measured here: the legacy console with a raster font -- declined, because getting into
one means changing the box's console font setting. The wrap boundary for wide characters is
no longer one of the gaps -- it is what `W8`'s tests cover, and the fix was watched on this
screen in both directions.

**The live test model (corrected -- the earlier version of this paragraph was wrong).**
`10.196.88.170:10001` lists, streams, reasons and calls tools, and both hosts are green
against it: macOS 216 passed / 0 failed, Windows 211 passed / 0 failed / 2 skipped. What
this paragraph said before was that the three live failures were the endpoint's doing, and
every part of that reasoning failed: the `curl` that "worked" was not sending atui's
`tools` array, so it differed from the failing request in exactly the one way that
mattered; and the parse error's column moving between runs is what a server reporting a
position in the **request** does when the request varies -- between tests, and between
hosts whose system prompts name different paths. The failures were atui's own. One
missing `}` in `run_command`'s argument schema, introduced by `ca4503e`, made every
request that offered a tool invalid JSON, and a server that parses its input said 500
while this document went looking for a fault in somebody else's machine. `W7` and
`TESTING.md` have the arc and the A/B.

**P5 -- Fill the Windows gaps.** Each behind its capability query, each measured
before it is claimed. Landed so far, as W1-W4 of the cross-platform work above: the
spawn deadline and the merged output (`W3`), the decision log rotating (`W2`), one
JSON decoder with working `\u` (`W1`), and the tool opening the path the policy
decided about (`W4`). Still to do, in the order the gap table gives them: symlink
containment (`W5`, which is also what stands between this and a green Windows
suite), `src/platform/screen_win.cpp` -- there is no capture at all today and CMake
has no `WIN32` branch for it, so the screenshot tool is simply absent -- plus
`clock.cpp`'s reentrant time calls, a console control handler so `Ctrl+C` does not
die mid-turn without closing the journal, and `.rc`/manifest handling, which is a
prerequisite for the XP subsystem version rather than a nicety. Symlink containment
is no longer in that list: `W5` closed it, and the Windows suite has no failures
left to close.

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
