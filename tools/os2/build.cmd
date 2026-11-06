@echo off
rem ====================================================================
rem  atui on OS/2 (Open Watcom 1.9) -- build driver.
rem
rem  This runs ON THE TARGET.  From the host, `tools/atui-remote build`
rem  mirrors the sources, runs this file, brings build.log back and exits with
rem  the status printed at the end of it.
rem
rem      build.cmd            tvision, then atui
rem      build.cmd tvision    library sources only
rem      build.cmd atui       the program only
rem      build.cmd one src\tui\theme.cpp
rem                           one file, then stop.  The port is being written
rem                           file by file, and this asks the cheap question --
rem                           does this file compile -- without waiting on 177
rem                           others.
rem      build.cmd tvone tvision\tvision\hardwvos2.cpp
rem                           one tvision library file, compiled into obj\tv so
rem                           that `lib` picks it up.  Same question as `one`,
rem                           other directory, and the fast way to test a driver
rem                           change: tvone, atui, exe.
rem      build.cmd exe candidate.exe
rem                           link the program under another name, so it can be
rem                           started while atui.exe is running at a screen.
rem      build.cmd lib       archive the library objects into obj\tvision.lib.
rem      build.cmd link hello.cpp hello.exe
rem                           compile one program and link it: %2 is the source,
rem                           %3 the executable.  A first link is the milestone
rem                           this exists for; the :link section says what it
rem                           asks of the toolchain.
rem      build.cmd exe       link atui.exe from the objects `build.cmd atui`
rem                           compiled, against obj\tvision.lib.  Run after it:
rem                           this mode links what is there rather than
rem                           compiling anything, which keeps the two questions
rem                           -- does it compile, does it link -- apart.
rem      build.cmd size      `atui` again with -os instead of -od, for the size
rem                           measurement; the :size section says how to ask it.
rem      build.cmd thrprobe probe_thread.cpp pthr.exe
rem                           does the C run time have threads?  Compiles and links one
rem                           probe twice, once plain and once `thrmt` with -bm, so the
rem                           two can be compared on the same afternoon.
rem      build.cmd netrun probe_turn.cpp pturn.exe
rem                           a probe linked against the objects `atui` compiled,
rem                           minus main.obj -- so it runs the program's own code
rem                           path from the shell, with no terminal to type at.
rem                           The :netrun section says why that is worth having.
rem      build.cmd atunon    `atui` with ATUI_HAVE_NETWORK absent: the
rem                           configuration for a machine with no TCP/IP stack, and
rem                           the one that proves the agent-mode code stayed inside
rem                           its ifdef.  Compile only -- `exe` links the network
rem                           build, and the two of them share obj\at, so run this
rem                           last if you want the object directory to hold what the
rem                           program was built from.
rem      build.cmd netlink probe_net.cpp probe_net.exe
rem                           `link` plus tcpip32.lib, for the socket probes.
rem                           Separate because an import library is a promise the
rem                           loader has to keep, so it is not something to link
rem                           into a program that never opens a socket.
rem      build.cmd netprobe probe_http.cpp probe_http.exe
rem                           A probe made of more than one file: compiles the
rem                           transport sources (net/http.cpp, net/sock_os2.cpp and
rem                           the core helper it needs) with the probe and links
rem                           them with tcpip32.lib.  `netlink` asks whether a file
rem                           compiles; this asks whether the transport works, and
rem                           that is a question about several files at once.
rem
rem  Why a batch file and not a command line: this target's command processor
rem  silently discards an input line longer than about 315 characters and says
rem  nothing at all.  A compile command carrying these switches is longer than
rem  that, so the loop lives here.
rem
rem  Why a loop over each directory and not a list of objects: the tree is 236
rem  files, two thirds of them tvision's, and a list would drift from the
rem  sources it describes.  "Everything in this directory" is what is meant, so
rem  "everything in this directory" is what is written.  The two trees keep
rem  separate object directories, because tvision's source\tvision\app.cpp and
rem  atui's src\tui\app.cpp both want to be app.obj: while nothing was being
rem  linked that collision was invisible, and it is the kind that costs an
rem  afternoon once something is.  The sources are 8.3-clean upstream.
rem
rem  Every file is compiled even after one fails.  At this stage the error
rem  landscape is the deliverable, and Watcom caps what it reports within one
rem  file by itself, which keeps the log readable.
rem
rem  Compiler output goes into build.log; one line per file goes to the screen.
rem  That split is not cosmetic: the host drives this over telnet and decides a
rem  build has finished by watching for output, so a build with every byte
rem  redirected is indistinguishable from a hung one.
rem ====================================================================

set WATCOM=C:\WATCOM
rem INCLUDE is set rather than extended.  The login environment also names the
rem IBM toolkit, which has its own sys\socket.h with different declarations;
rem mixing them is silent and fatal, so nothing outside Watcom's own two
rem directories is allowed to be found by accident.
set INCLUDE=C:\WATCOM\H\OS2;C:\WATCOM\H
set LIB=C:\WATCOM\LIB386\OS2

rem Switch drives first.  The tree lives on C: (the box's prompt reads
rem [<GPCVM>-C:\] and the root is C:\atui); the cd is here so the batch lands
rem in the project whatever drive the login happened to start on.  The drive
rem letter is the one thing this file cannot learn by looking around, so it is
rem written down; REMOTE_ROOT in tools/atui-remote says the same thing.
c:
cd \atui
if not exist obj mkdir obj
if not exist obj\tv mkdir obj\tv
if not exist obj\at mkdir obj\at
rem One build, one log.  `tools/atui-remote build` waits for the ATUI-BUILD-RC line
rem at the end of this file to decide how the build went, so a previous run's line
rem will answer for a build that has not finished -- or, measured, for an `exe` link
rem that never ran at all: rc=0 and 172 lines of the build before it.
set LOG=build.log
if exist %LOG% del %LOG%

rem The compiler switches live in defs.rsp rather than in a variable here,
rem because OS/2's SET silently refuses a value containing '=' -- it stores
rem nothing, reports nothing, and the reference later expands to empty, so the
rem compile line starts with -fo=... and cmd tries to run THAT as a program.
rem defs.rsp holds -bt=os2 -5 -od -w3, the include paths and the defines:
rem
rem   -bt=os2  32-bit OS/2 target.  Note the LINKER spells the same choice
rem            "system os2v2"; plain "system os2" there asks for 16-bit.
rem   -5       Pentium code, because that is what the box is.
rem   -od      no optimisation while nothing compiles; -os returns when the
rem            binary has to be small.
rem   -w3      warnings visible.  A port that does not read them ships them.
rem
rem Nothing else goes in defs.rsp: it is a Watcom response file, and those have
rem no comment syntax -- a '#' at the start of a line is taken for a file name
rem and the compile dies with "unable to open '#'" (measured).  So the notes
rem about its contents live here instead.
rem
rem The include paths mirror tv_add_private_includes() in
rem tvision/CMakeLists.txt, which is how tvision's own sources are compiled:
rem
rem   compat\borland   the emulated Borland RTL (dir.h, io.h, malloc.h,
rem                    iostream.h, ...).  The classic layer includes those
rem                    names directly -- the README says its source still
rem                    depends on them.
rem   compat\windows   BOOL, DWORD, CHAR_INFO and friends, which hardware.h
rem                    includes whenever __FLAT__ is defined; Watcom defines
rem                    __FLAT__ by itself in a flat-model compile.
rem
rem CMake's fourth directory, compat\malloc, is deliberately NOT here.  It exists
rem for systems whose C library has no malloc.h (macOS, mainly); Open Watcom has
rem one, and putting the shim on the include path only hides the real one -- and
rem the shim cannot even compile here, because it asks whether malloc_np.h exists
rem with __has_include, which is C++17 (measured: E767 "unmatched left
rem parenthesis" from inside the shim).
rem
rem No -D switch here changes what language or colour model is compiled: which
rem dialect and which colour model tvision uses is decided inside tvision from
rem the compiler's own macros, the way it already decides them for Borland, and a
rem build line that can overturn that is a build line that quietly builds a
rem different library.
rem
rem One -D switch is here that is not about the language: INCL_DOS.  <os2.h> is a
rem three-line file that pulls in os2def.h (typedefs), bse.h and pm.h; the kernel
rem calls themselves are in bsedos.h, and each group of them -- semaphores,
rem process, file manager, profile (where DosTmrQueryTime lives) -- is behind an
rem INCL_DOSxxx macro (measured: DosCreateMutexSem, DosSleep, DosTmrQueryFreq and
rem friends are all "has not been declared" until a flag says otherwise).
rem INCL_DOS is the one macro that switches all of those groups on at once, and
rem it belongs here rather than in whichever header wants os2.h first: a header is
rem read once per compilation, so a flag set later than somebody else's
rem #include <os2.h> arrives too late to do anything -- which is exactly the
rem situation once atui's sources and tvision's driver both want the kernel.
set WPP=wpp386 @defs.rsp

rem The resource compiler.  The Warp 4 Toolkit copy the first PM probe was
rem verified against lived on D:, and the whole drive is gone: the box moved
rem (2026-11-01, now 10.196.88.148 with the tree on C:\atui) and what remains
rem is the system RC/2 4.00.011 (its own /? says so), which understands the
rem headerless numeric dialect atuipm.rc is written in.  The pmexe mode is the
rem proof of that dialect; the toolkit copy stays the reference if it ever
rem disagrees.
set RCPROG=c:\os2\rc.exe

set RC=0

if "%1"=="one" goto one
if "%1"=="tvone" goto tvone
if "%1"=="lib" goto lib
if "%1"=="link" goto link
if "%1"=="exe" goto exe
if "%1"=="netlink" goto netlink
if "%1"=="netprobe" goto netprobe
if "%1"=="netall" goto netall
if "%1"=="netrun" goto netrun
if "%1"=="thrprobe" goto thrprobe
if "%1"=="thrmt" goto thrmt
if "%1"=="thread" goto thread
if "%1"=="size" goto size
if "%1"=="atui" goto atui
if "%1"=="atunon" goto atunon
if "%1"=="pmdrv" goto pmdrv
if "%1"=="pmlib" goto pmlib
if "%1"=="pmexe" goto pmexe

echo ================== tvision library ==================
rem Only source/tvision, and not geninc.cpp.  This is what the Borland build in
rem tvision/project/makefile compiles -- 175 of the 178 files there, the three
rem exceptions being hardware.cpp (the modern implementation lives in
rem source/platform), swapst.cpp and sysint.cpp -- and it is not an accident of
rem history that the old-compiler build stops at this directory.
rem
rem source/platform is the terminal layer for C++14 compilers: display buffers,
rem the event waiter, the ANSI writer, the UTF-8 text engine.  An old compiler
rem does not build it and does not need it, because tvision's headers carry a
rem second, complete implementation of everything the classic classes require
rem (ttext.h inlines the single-byte text engine, colors.h and scrncell.h
rem substitute the BIOS colour model, config.h sets maxCharSize to 1).  Those
rem branches are what an OS/2 build joins; bending the modern layer to C++98
rem would mean maintaining a third version of all of it.
rem
rem geninc.cpp is a program that generates tables for the DOS driver at build
rem time; CMake leaves it out of the library too.  Excluding it compares whole
rem paths, because this command processor has neither the `if /I` switch nor the
rem `%%~n` name modifiers of NT's cmd -- `%%~nf` comes out of it as the literal
rem text `%~nf`, and `if /I not` dies with SYS1079 "not was unexpected at this
rem time" (both measured with a two-form test batch).
for %%f in (tvision\source\tvision\*.cpp) do if not "%%f"=="tvision\source\tvision\geninc.cpp" (echo [tv] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\tv\ %%f >> %LOG% 2>&1 || set RC=1)

rem Archiving is part of building the library: stopping at the objects leaves
rem obj\tvision.lib from the previous run in place, and a link then reports the
rem *last* build's undefined symbols -- identical output, no sign that the fix
rem under test was never tried.  Measured the hard way on a 53-symbol list.
if "%1"=="tvision" goto lib

:atui
echo ================== atui ==================
rem The shipping configuration on this platform: agent mode, on atui's own HTTP
rem transport over tcpip32.lib.  This is what -DATUI_HAVE_NETWORK=ON means in
rem CMake, and the define goes on EVERY source, not just the transport's -- the
rem TUI, the settings and the tools all read it, because a build that cannot ask a
rem model must not offer, default to or display agent mode.  Passing it to one file
rem and not the others is how the two halves of one program could disagree about
rem whether the network exists.
set NET=-DATUI_HAVE_NETWORK
goto atui_common

rem The same sources with the define absent: the configuration a machine with no
rem TCP/IP stack gets, and the one the requirements keep as a test harness.  It has
rem to keep compiling, because "it also builds without the network" is the check
rem that the agent-mode code did not leak out of its ifdef.  Compile only: it is not
rem linked, and `exe` links the obj\at that the network build put there.
rem
rem NET is given a token rather than left empty on purpose.  An undefined variable
rem does not expand the same way on every command processor, and this file has
rem already been fooled once by a build that reported OK after compiling nothing;
rem nothing reads ATUI_NONET, and what it buys is that no line here depends on how
rem an unset name happens to expand.
:atunon
echo ================== atui, no network ==================
set NET=-DATUI_NONET
:atui_common
rem Compiled from scratch every time, and the objects cleared first: `exe` links
rem whatever obj\at holds, so an object left behind by a file that is no longer
rem part of the tree would go into the program instead of being missed.
if not exist obj\at mkdir obj\at
del obj\at\*.obj >> %LOG% 2>&1
for %%f in (src\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% %NET% -fo=obj\at\ %%f >> %LOG% 2>&1 || set RC=1)
for %%f in (src\core\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% %NET% -fo=obj\at\ %%f >> %LOG% 2>&1 || set RC=1)
rem The transport: atui's own HTTP over BSD sockets, the same 15 files every other
rem platform compiles.  This used to be `@atui_net.rsp`, a hand-written list, because
rem three files in the directory could not be built here at all (the libcurl client
rem and the worker-thread hand-off it left behind).  They are gone, so the list is
rem gone with them: a list of files that happens to equal the directory is a way of
rem forgetting a file, and a source file that is not compiled on this target is a
rem bug that only appears as a link error -- or, worse, as an older object that still
rem works.  A `for` over a wildcard needs no `call :label` here (netall documents why
rem that combination does not work when skipping is required), so this is the plain
rem loop the other directories use.
for %%f in (src\net\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% %NET% -fo=obj\at\ %%f >> %LOG% 2>&1 || set RC=1)
for %%f in (src\settings\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% %NET% -fo=obj\at\ %%f >> %LOG% 2>&1 || set RC=1)
for %%f in (src\platform\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% %NET% -fo=obj\at\ %%f >> %LOG% 2>&1 || set RC=1)
for %%f in (src\tools\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% %NET% -fo=obj\at\ %%f >> %LOG% 2>&1 || set RC=1)
for %%f in (src\tui\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% %NET% -fo=obj\at\ %%f >> %LOG% 2>&1 || set RC=1)
if "%RC%"=="1" goto one_failed
echo [at] objects out:
dir obj\at\*.obj | find "file(s)"
dir obj\at\*.obj | find "file(s)" >> %LOG%

:lib
rem The archive a program links against.  wlib's usage lists no response file,
rem and the command processor could not carry 177 object names on one line in
rem any case, so the library is grown one object per call: slower than one
rem invocation and completely adequate for something that runs when the library
rem changes.
if exist obj\tvision.lib del obj\tvision.lib
echo [lib] growing obj\tvision.lib from obj\tv\*.obj
for %%f in (obj\tv\*.obj) do wlib -q obj\tvision.lib +%%f >> %LOG% 2>&1
if not exist obj\tvision.lib goto one_failed
wlib -q -l=obj\tv\liblist.txt obj\tvision.lib >> %LOG% 2>&1
dir obj\tvision.lib
rem What went into it.  The listing wlib writes names a symbol per line with the
rem module it came from -- it has no file names in it, so counting \".obj\" there
rem finds nothing, which is a confusing way to report success.
dir obj\tv\*.obj | find "file(s)"
echo [lib] OK
goto summary

:link
rem Compile one program and link it: %2 is the source, %3 the executable.  What
rem a first link asks of a toolchain, and none of it is answered by a compile:
rem whether `system os2v2` yields a character-mode program a telnet session can
rem run, whether the libraries the linker picks by itself satisfy a C++
rem program's startup, and how large the result is.  The object list goes into a
rem response file a line at a time -- wlink documents "@directive_file", and a
rem "#" at the start of a line is its comment, which wpp386 response files have
rem no notion of.
echo [link] %2 to %3
rem The object goes into a scratch directory of its own and the link takes
rem everything in it: pointing at obj\at instead would link whatever else had
rem been compiled there, and a second main() is not a linker error worth
rem spending an afternoon on.
echo [link] %2 to %3
if not exist obj\one mkdir obj\one
if exist obj\one\*.obj del obj\one\*.obj
%WPP% -fo=obj\one\ %2 >> %LOG% 2>&1
if errorlevel 1 goto one_failed
if exist link.rsp del link.rsp
echo system os2v2 > link.rsp
echo name %3 >> link.rsp
echo op m >> link.rsp
rem  A stack size of our own, set while a "Stack Overflow!" from the target was
rem  being run down.  It was not the stack: doubling it changed nothing, because
rem  the recursion was an unbounded one in the screen cell's own assignment
rem  operator (fork ab73350), and the message was the target's, not tvision's.
rem  It stays because the default for a character-mode OS/2 program was never
rem  measured and tvision's drawing walks the view tree by recursion, and 256 KB
rem  is nothing to ask of a machine that has 300 MB free.
echo option stack=256k >> link.rsp
for %%f in (obj\one\*.obj) do echo file %%f >> link.rsp
echo library obj\tvision.lib >> link.rsp
echo library os2386.lib >> link.rsp
rem  stubl.lib is the startup library every OW makefile links ("wlinksupport"):
rem  the C runtime opens with a deliberate software fault (XCPT_SOFTWARE_FAULT,
rem  0x60068 -- the banner/callout to the debugger) whose handler lives there.
rem  Without it the fault sails past first chance: harmless alone, fatal under
rem  any debugger (measured: probe_busy under prof died with EXC 1/2 60068
rem  2026-10-26, which is also why wsample killed candidate7 -- same code path,
rem  no stub in either link).
echo library stubl.lib >> link.rsp
wlink @link.rsp >> %LOG% 2>&1
if errorlevel 1 goto one_failed
dir %3
echo [link] OK
goto summary

:netlink
rem `link` with the TCP/IP import library added, which is what the socket probes
rem need and nothing else wants.  It is a separate mode because an import library
rem is not a passive thing: linking one records the DLL's entry points in the
rem image, so a program that never opened a socket would still require the
rem TCP/IP stack at load time.  %2 is the source, %3 the executable.
echo [netlink] %2 to %3
if not exist obj\one mkdir obj\one
if exist obj\one\*.obj del obj\one\*.obj
%WPP% -fo=obj\one\ %2 >> %LOG% 2>&1
if errorlevel 1 goto one_failed
if exist link.rsp del link.rsp
echo system os2v2 > link.rsp
echo name %3 >> link.rsp
echo op m >> link.rsp
echo option stack=256k >> link.rsp
for %%f in (obj\one\*.obj) do echo file %%f >> link.rsp
echo library tcpip32.lib >> link.rsp
echo library os2386.lib >> link.rsp
wlink @link.rsp >> %LOG% 2>&1
if errorlevel 1 goto one_failed
dir %3
echo [netlink] OK
goto summary

:netprobe
rem A probe of the transport, which is several sources.  The point is not that each
rem file compiles on its own -- that is what `one` is for -- but that they compile
rem together with Watcom, link against the stack, and reach a real server through
rem the OS/2 spellings: soclose, sock_errno, FIONBIO, and a connect() whose return
rem value means nothing.  %2 is the probe source, in the root; %3 the executable.
echo [netprobe] %2 to %3
if not exist obj\np mkdir obj\np
if exist obj\np\*.obj del obj\np\*.obj
for %%f in (src\net\http.cpp src\net\sock_os2.cpp src\core\strutil.cpp %2) do (echo [netprobe] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\np\ %%f >> %LOG% 2>&1 || set RC=1)
rem Not `NEQ`: this command processor answers `SYS1079: NEQ was unexpected
rem at this time`, and it is a whole-line failure -- the batch stops there, between
rem the last compile and the link, which is a confusing place to be.  RC is only
rem ever 0 or 1, so test for the 1.
if "%RC%"=="1" goto one_failed
if exist link.rsp del link.rsp
echo system os2v2 > link.rsp
echo name %3 >> link.rsp
echo op m >> link.rsp
echo option stack=256k >> link.rsp
for %%f in (obj\np\*.obj) do echo file %%f >> link.rsp
echo library tcpip32.lib >> link.rsp
echo library os2386.lib >> link.rsp
wlink @link.rsp >> %LOG% 2>&1
if errorlevel 1 goto one_failed
dir %3
echo [netprobe] OK
goto summary

:thrprobe
rem Is there a C run time with threads on this target?  Ask the libraries, twice: once
rem with the plain runtime, once with -bm.  `%THRF%` is always given a value -- an
rem otherwise meaningless define here -- because this command processor expands an
rem undefined variable as its own name, which is how %NET% once handed the compiler a
rem file called "%NET%".
set THRF=-DATUI_THREAD_PROBE_PLAIN
set THROBJ=obj\nt1
set THRTAG=plain runtime
goto thrbuild

:thrmt
set THRF=-bm -DATUI_HAVE_OS2_THREADS
set THROBJ=obj\nt2
set THRTAG=-bm
goto thrbuild

:thrbuild
rem %2 the probe source, %3 the executable.  The probe itself says what it checks and
rem what a failure would mean; tools/os2/probe_thread.cpp.
echo [thrbuild] %2 to %3 (%THRTAG%)
if not exist %THROBJ% mkdir %THROBJ%
if exist %THROBJ%\*.obj del %THROBJ%\*.obj
echo ---- thrbuild compile %THRTAG% ---- >> %LOG%
%WPP% %THRF% -fo=%THROBJ%\ %2 >> %LOG% 2>&1
if errorlevel 1 goto one_failed
if exist link.rsp del link.rsp
echo system os2v2 > link.rsp
echo name %3 >> link.rsp
echo op m >> link.rsp
echo option stack=256k >> link.rsp
for %%f in (%THROBJ%\*.obj) do echo file %%f >> link.rsp
echo library os2386.lib >> link.rsp
wlink @link.rsp >> %LOG% 2>&1
if errorlevel 1 goto one_failed
dir %3
echo [thrbuild] OK %THRTAG%
goto summary

:size
rem The same sources as `atui`, compiled -os (optimize for size) rather than -od.
rem This is a measurement mode, not a shipping configuration: -od is the honest
rem default while a port is being written, and the map file says the program is
rem 904 KB of code with it, so the question -- what does the compiler give back
rem when asked for size -- is worth answering with two executables rather than an
rem opinion.  Ask it like this, and `dir atui*.exe` is the answer:
rem
rem     build.cmd exe                    and then:  copy atui.exe atui_od.exe
rem     build.cmd size
rem     build.cmd exe
rem
echo ================== atui, -os ==================
set WPP=%WPP% -os
goto atui

:exe
rem atui itself, linked.  Run after `build.cmd atui`, which is what puts the
rem objects in obj\at; that directory holds only atui's own translation units
rem (tvision's go to obj\tv), so naming all of them is the whole program and not
rem an accident with two main()s.  The directives are the ones :link measured --
rem `system os2v2` for a character-mode program a telnet session can run, `op m`
rem for the map file a size question is answered from, and os2386.lib because
rem Watcom does not link the OS/2 kernel by default and compat/'s mutex calls
rem DosCreateMutexSem out of it.
echo ================== atui.exe ==================
if not exist obj\at\main.obj goto one_failed
rem The image name can be given (`build.cmd exe candidate.exe`) so a candidate can
rem be linked and started while atui.exe is running at some screen: the linker
rem cannot write an image that is loaded, and its `Permission denied` reads like a
rem build failure to anyone going through build.log.
set EXENAME=atui.exe
if not "%2"=="" set EXENAME=%2
if exist link.rsp del link.rsp
echo system os2v2 > link.rsp
echo name %EXENAME% >> link.rsp
echo op m >> link.rsp
echo option stack=256k >> link.rsp
for %%f in (obj\at\*.obj) do echo file %%f >> link.rsp
echo library obj\tvision.lib >> link.rsp
echo library os2386.lib >> link.rsp
rem tcpip32.lib, because `atui` is a network build: the socket calls resolve to
rem sock_os2.cpp's declarations and this import library.  Linking it records the
rem stack's entry points in the image, so the program needs TCP/IP present at load
rem time even if it never opens a socket -- which is the right trade for a program
rem whose only mode is asking a model something over the LAN, and the reason the
rem no-network configuration above is compile-checked rather than linked here.
echo library tcpip32.lib >> link.rsp
rem  stubl.lib: the startup library that swallows the runtime's opening
rem  software-fault banner notification (note in :link).  Every OW makefile
rem  links it; without it the program is hostile to every debugger.
echo library stubl.lib >> link.rsp
wlink @link.rsp >> %LOG% 2>&1
if errorlevel 1 goto one_failed
dir %EXENAME%
echo [exe] OK
goto summary

:pmdrv
rem The PM display driver, and only it.  ATUI_PM is what tells it apart:
rem hardwspm.cpp's own guard tests the define, so a `lib` build of the whole
rem tree compiles the file to an empty object and the VIO library keeps
rem exactly one driver.  It goes to obj\pm rather than obj\tv for the same
rem reason `tvone` exists -- the splice below wants it where the ordinary
rem library build will not sweep it up.
echo ================== hardwspm.obj ==================
if not exist obj\pm mkdir obj\pm
%WPP% -DATUI_PM -fo=obj\pm\ tvision\source\tvision\hardwspm.cpp >> %LOG% 2>&1
if errorlevel 1 goto tvone_failed
rem The native dialogs ride the same mark (ATUI_PM) for the same reason:
rem they speak PM and must be inert everywhere else.
%WPP% -DATUI_PM -fo=obj\pm\ tvision\source\tvision\pmdlg.cpp >> %LOG% 2>&1
if errorlevel 1 goto tvone_failed
echo [pmdrv] OK
goto summary

:pmlib
rem The PM tvision library: a copy of obj\tvision.lib with its display
rem driver swapped -- hardwvos2.obj out, hardwspm.obj in.  Spliced from the
rem copy rather than rebuilt from 177 sources because the two libraries
rem differ in exactly one member; and it has to be a swap because both
rem drivers in one link is a duel of duplicated symbols the linker resolves
rem in whichever order it likes -- an invisible loss whose only sign is a
rem PM image doing VIO calls.  The listing check after is not decoration:
rem a `wlib -name` that matched nothing is a silent success otherwise.
echo ================== obj\tvisionpm.lib ==================
if not exist obj\tvision.lib goto one_failed
if not exist obj\pm\hardwspm.obj goto one_failed
if not exist obj\pm\pmdlg.obj goto one_failed
if exist obj\tvisionpm.lib del obj\tvisionpm.lib
copy obj\tvision.lib obj\tvisionpm.lib >> %LOG% 2>&1
if not exist obj\tvisionpm.lib goto one_failed
wlib -q obj\tvisionpm.lib -hardwvos2 +obj\pm\hardwspm.obj +obj\pm\pmdlg.obj >> %LOG% 2>&1
if errorlevel 1 goto one_failed
wlib -q -l=obj\pm\liblist_pm.txt obj\tvisionpm.lib >> %LOG% 2>&1
find /i "hardwvos2" obj\pm\liblist_pm.txt
if not errorlevel 1 goto pmlib_both
find /i "hardwspm" obj\pm\liblist_pm.txt >> %LOG% 2>&1
if errorlevel 1 goto pmlib_both
find /i "pmdlg" obj\pm\liblist_pm.txt >> %LOG% 2>&1
if errorlevel 1 goto pmlib_both
dir obj\tvisionpm.lib
echo [pmlib] OK
goto summary
:pmlib_both
echo [pmlib] FAILED: the splice did not take -- listing both drivers, or neither
goto one_failed

:pmexe
rem The PM image: the same application objects `exe` links -- the program
rem does not know which driver sits under it, that is the THardwareInfo
rem seam's whole point -- with the driver-swapped library and `system
rem os2v2_pm`, the recipe settled by the probe.  Then rc: a Watcom PM image
rem carries no resource segment until rc appends one, and WinCreateStdWindow
rem answers that with PMERR_RESOURCE_NOT_FOUND; atuipm.rc is the icon the
rem frame asks for and nothing else -- atui draws its own menu bar.
rem Like every PM image this one must never be started from a telnet
rem session; it hangs that session.  Start it at the box's own display, and
rem read atuipm.run.txt afterwards -- it is written as the window is born.
echo ================== atuipm.exe ==================
if not exist obj\at\main.obj goto one_failed
if not exist obj\tvisionpm.lib goto one_failed
set EXENAME=atuipm.exe
if not "%2"=="" set EXENAME=%2
if exist link.rsp del link.rsp
echo system os2v2_pm > link.rsp
echo name %EXENAME% >> link.rsp
echo op m >> link.rsp
echo option stack=256k >> link.rsp
for %%f in (obj\at\*.obj) do echo file %%f >> link.rsp
echo library obj\tvisionpm.lib >> link.rsp
echo library os2386.lib >> link.rsp
echo library tcpip32.lib >> link.rsp
echo library stubl.lib >> link.rsp
wlink @link.rsp >> %LOG% 2>&1
if errorlevel 1 goto one_failed
%RCPROG% -n -p atuipm.rc %EXENAME% >> %LOG% 2>&1
if errorlevel 1 goto one_failed
dir %EXENAME%
echo [pmexe] OK: never start it from telnet; run it at the box display
goto summary

:one
rem One file, with the environment set up above.  The switches come from
rem defs.rsp, which is what keeps this command line inside the length the
rem command processor is willing to read at all.  Control jumps to labels rather
rem than using a parenthesised block, because `if errorlevel` reads whatever the
rem last command left behind and a block invites questions about which command
rem that was.
rem
rem Compiled as `atui` compiles everything else, network define included, because
rem the object lands in obj\at beside the ones it gets linked with.  Without it a
rem `one` + `exe` produced a program that started, drew its screen and answered
rem every question with "this build of atui has no network transport" -- it links,
rem it runs, and only the answer is wrong, which is the worst shape a build
rem mistake can take.  The no-network configuration belongs to `atunon`, which
rem rebuilds every file rather than one.
set NET=-DATUI_HAVE_NETWORK
echo [one] %2
echo ---- %2 ---- >> %LOG%
%WPP% %NET% -fo=obj\at\ %2 >> %LOG% 2>&1
if errorlevel 1 goto one_failed
echo [one] OK
goto summary
:one_failed
set RC=1
echo [one] FAILED
goto summary

:tvone
rem One tvision library source, compiled into obj\tv where `lib` will find it.
rem `one` is the app's mode and puts its object in obj\at; a library file needs the
rem other directory or the archive keeps the previous object and the link reports
rem the last build's behaviour.  No network define: none of tvision reads it, and
rem `tvision` compiles this directory without it.
rem
rem What it is for: one file of 177 in a five minute library build.  The sequence
rem after changing a single library source is tvone, atui, exe -- about a minute
rem and a half, against four and a half for the whole library.
echo [tvone] %2
echo ---- %2 ---- >> %LOG%
if not exist obj\tv mkdir obj\tv
%WPP% -fo=obj\tv\ %2 >> %LOG% 2>&1
if errorlevel 1 goto tvone_failed
echo [tvone] OK
goto summary
:tvone_failed
set RC=1
echo [tvone] FAILED
goto summary

rem ---------------------------------------------------------------------------
rem netall: compile the network layer and everything it reaches, with no program
rem at the end.  This is not a link test and not a run test -- `netprobe` did those,
rem against a real server -- it is the cheap question asked early: do the rest of the
rem sources (the SSE framing, the endpoint splitter, the chat request, the chat turn,
rem the blocking face, the permission agent) survive wpp386 at all, and with what
rem warnings.  Worth asking before the TUI is rewired to use them, because the answer
rem so far has been that a file which compiles under gcc's C++98 mode does not
rem necessarily compile here, and the failures are cheap in isolation and expensive
rem in a link of 200 objects.
rem
rem Nothing in src\net is skipped any more: the libcurl client and the worker-thread
rem hand-off that could not be built here were deleted once the TUI stopped using
rem them.  The ATUI_HAVE_NETWORK define is what the CMake build sets for the same
rem sources, so the conditional code being compiled is the code the other platforms
rem ship.
rem ---------------------------------------------------------------------------
rem thread: the thread-capability probe.  %2 the probe source in the root, %3 the
rem executable.  Compiled -bm (multithreaded C runtime): without it two threads
rem share one heap and the probe's heap test is really a corruption test whose
rem symptoms show up later somewhere else.  `-mt` does not exist here -- measured,
rem E068 "invalid option" with N746 naming the switch, which is why the first run
rem of this mode looked like a source error rather than a missing option.
:thread
echo [thread] %2 to %3
if not exist obj\th mkdir obj\th
if exist obj\th\*.obj del obj\th\*.obj
echo ---- thread ---- >> %LOG%
%WPP% -bm -fo=obj\th\ %2 >> %LOG% 2>&1
if errorlevel 1 goto one_failed
rem The library's own OS/2 threading implementation, compiled the way a threaded build
-- compiles it: -bm for the runtime and ATUI_HAVE_OS2_THREADS to say so, because the
-- compiler's macros cannot tell the two C runtimes apart (core/thread.h).  Compile
-- only: this says the code is Watcom's language and API, not that the program runs,
-- and the probe above is what measures the running.
for %%f in (src\core\thread.cpp src\core\thread_os2.cpp src\core\thread_none.cpp) do (echo [thread] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -bm -DATUI_HAVE_OS2_THREADS -fo=obj\th\ %%f >> %LOG% 2>&1 || set RC=1)
if "%RC%"=="1" goto one_failed
if exist link.rsp del link.rsp
echo system os2v2 > link.rsp
echo name %3 >> link.rsp
echo op m >> link.rsp
echo option stack=256k >> link.rsp
for %%f in (obj\th\*.obj) do echo file %%f >> link.rsp
echo library tcpip32.lib >> link.rsp
echo library os2386.lib >> link.rsp
wlink @link.rsp >> %LOG% 2>&1
if errorlevel 1 goto one_failed
dir %3
echo [thread] OK
goto summary


:netrun
rem A probe against the program's own objects.  `netprobe` builds a probe out of three
rem transport files, which is right for asking whether the stack works; this answers a
rem different question -- why the *application's* request failed -- and to answer that
rem honestly the probe has to use the application's request builder, settings snapshot,
rem system prompt and tool list.  Copying those into a probe would produce a second
rem implementation, which is the thing this port keeps trying to get rid of.
rem
rem So: link obj\at (what `atui` compiled), except main.obj, because that is where
rem main() lives and two of them is one too many.  It means `atui` must be run first,
rem and that obj\at is the network build -- the same caveat `exe` carries.
echo [netrun] %2 to %3
if not exist obj\nr mkdir obj\nr
if exist obj\nr\*.obj del obj\nr\*.obj
echo [netrun] compiling %2
echo ---- netrun compile ---- >> %LOG%
%WPP% -DATUI_HAVE_NETWORK -fo=obj\nr\ %2 >> %LOG% 2>&1
if errorlevel 1 goto one_failed
if exist link.rsp del link.rsp
echo system os2v2 > link.rsp
echo name %3 >> link.rsp
echo op m >> link.rsp
echo option stack=256k >> link.rsp
for %%f in (obj\nr\*.obj) do echo file %%f >> link.rsp
for %%f in (obj\at\*.obj) do if not "%%f"=="obj\at\main.obj" echo file %%f >> link.rsp
echo library obj\tvision.lib >> link.rsp
echo library tcpip32.lib >> link.rsp
echo library os2386.lib >> link.rsp
wlink @link.rsp >> %LOG% 2>&1
if errorlevel 1 goto one_failed
dir %3
echo [netrun] OK
goto summary

:netall
echo [netall] compiling the network layer
if not exist obj\na mkdir obj\na
if exist obj\na\*.obj del obj\na\*.obj
echo ---- netall ---- >> %LOG%
rem One invocation over a response file of names (netall.rsp, synced to the root).
rem Not a `for` loop over a wildcard with a `call :label` subroutine to skip the
rem exceptions: this command processor matches wildcards fine (probe_for.cmd shows
rem `for %%f in (src\net\*.cpp)` listing eleven files) but does NOT run a `call
rem :label` subroutine -- the body never executed, `set RC=7` inside it never
rem happened, and the loop above quietly compiled zero files while netall reported
rem OK.  A success that means "nothing happened" is the worst thing a build driver
rem can do, so the exclusions live in the list instead of in skip logic, and the
rem object count is echoed because it is the evidence that files went in.
rem
rem netall.rsp is every net/core/tools source, minus main.cpp, which is not wanted in
rem a compile-only pass because it would drag the TUI in with it.
rem The three sock_*.cpp files are all listed on purpose: each is empty unless its
rem own platform macro is set, so compiling all three is also a check that those
rem guards hold.  Keep the list in step with CMakeLists.txt.
%WPP% -DATUI_HAVE_NETWORK -fo=obj\na\ @netall.rsp >> %LOG% 2>&1
if errorlevel 1 set RC=1
echo [netall] objects out:
dir obj\na\*.obj | find "file(s)"
dir obj\na\*.obj | find "file(s)" >> %LOG%
if "%RC%"=="1" goto netall_failed
echo [netall] OK
goto summary
:netall_failed
echo [netall] FAILED
goto summary



:summary
echo ================== summary ==================
dir obj\*.obj | find "file(s)"
dir obj\*.obj | find "file(s)" >> %LOG%
rem The verdict the host build loop reads.  Written by the batch rather than
rem inferred from the shell, because this driver carries on past a failing
rem compile, so the shell would report only the last file's status and call the
rem build good.
rem
rem And the verdict cannot come from an exit code either, which is not theory: a
rem build whose response-file compile printed `src\net\http.cpp: 666 lines, included
rem 8935, no warnings, 1 error` still reported ATUI-BUILD-RC=0, because the error
rem level after a compile of several files belongs to whoever touched it last.  A
rem driver that says OK about a file that did not compile is worse than one that
rem stops, so the log is asked directly: `Error!` is the marker both wpp386 and
rem wlink put on every error line they write, and a log containing it is a failed
rem build whatever the driver believes.
find "Error!" %LOG% >nul && set RC=1
echo ATUI-BUILD-RC=%RC%
echo ATUI-BUILD-RC=%RC% >> %LOG%
rem Whose build this was.  The console on this box is one command processor with
rem one input queue, and what is typed while it is busy waits there: a build sent
rem an hour ago was seen to run under a later visit and report its rc as the later
rem build's, complete with a log about files that had not been compiled.  So the
rem host sends a nonce as the fourth argument, and a log that does not carry it is
rem somebody else's build however plausible its verdict looks.
rem Whose build this was -- the nonce the host passed in ATUI_BUILD_ID, echoed
rem into the log and then forgotten, so that a later run in the same session
rem cannot pick it up and lend it to a build it does not belong to.  It travels
rem in the environment rather than as an argument because this command processor
rem cannot find the last of its own arguments: no substring expansion, and a
rem fixed position is only correct for the modes that take that many.
echo ATUI-BUILD-ID=%ATUI_BUILD_ID%
echo ATUI-BUILD-ID=%ATUI_BUILD_ID% >> %LOG%
set ATUI_BUILD_ID=
