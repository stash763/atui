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
rem      build.cmd lib       archive the library objects into obj\tvision.lib.
rem      build.cmd link hello.cpp hello.exe
rem                           compile one program and link it: %2 is the source,
rem                           %3 the executable.  A first link is the milestone
rem                           this exists for; the :link section says what it
rem                           asks of the toolchain.
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

set WATCOM=D:\WATCOM
rem INCLUDE is set rather than extended.  The login environment also names the
rem IBM toolkit, which has its own sys\socket.h with different declarations;
rem mixing them is silent and fatal, so nothing outside Watcom's own two
rem directories is allowed to be found by accident.
set INCLUDE=D:\WATCOM\H\OS2;D:\WATCOM\H
set LIB=D:\WATCOM\LIB386\OS2

cd \atui
if not exist obj mkdir obj
if not exist obj\tv mkdir obj\tv
if not exist obj\at mkdir obj\at
if exist build.log del build.log
set LOG=build.log

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
rem No -D switches here for the same reason: which language dialect and which
rem colour model tvision uses is decided inside tvision from the compiler's own
rem macros, the way it already decides them for Borland, and a build line that
rem can overturn that is a build line that quietly builds a different library.
set WPP=wpp386 @defs.rsp

set RC=0

if "%1"=="one" goto one
if "%1"=="lib" goto lib
if "%1"=="link" goto link
if "%1"=="atui" goto atui

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
for %%f in (src\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\at\ %%f >> %LOG% 2>&1 || set RC=1)
for %%f in (src\core\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\at\ %%f >> %LOG% 2>&1 || set RC=1)
rem The curl-backed transport is not part of this build: there is no libcurl
rem for Open Watcom.  That mirrors -DATUI_HAVE_NETWORK=OFF in CMake, which is
rem the configuration the requirements describe as a test harness -- so this
rem build must not advertise agent mode either, which is what that option
rem already arranges.  The socket transport that replaces it (Watcom has BSD
rem sockets and tcpip32.lib) is port work still to do.
for %%f in (src\net\provider.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\at\ %%f >> %LOG% 2>&1 || set RC=1)
for %%f in (src\settings\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\at\ %%f >> %LOG% 2>&1 || set RC=1)
for %%f in (src\tools\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\at\ %%f >> %LOG% 2>&1 || set RC=1)
for %%f in (src\tui\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\at\ %%f >> %LOG% 2>&1 || set RC=1)

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
echo [link] %2 -> %3
rem The object goes into a scratch directory of its own and the link takes
rem everything in it: pointing at obj\at instead would link whatever else had
rem been compiled there, and a second main() is not a linker error worth
rem spending an afternoon on.
echo [link] %2 -> %3
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
wlink @link.rsp >> %LOG% 2>&1
if errorlevel 1 goto one_failed
dir %3
echo [link] OK
goto summary

:one
rem One file, with the environment set up above.  The switches come from
rem defs.rsp, which is what keeps this command line inside the length the
rem command processor is willing to read at all.  Control jumps to labels rather
rem than using a parenthesised block, because `if errorlevel` reads whatever the
rem last command left behind and a block invites questions about which command
rem that was.
echo [one] %2
echo ---- %2 ---- >> %LOG%
%WPP% -fo=obj\at\ %2 >> %LOG% 2>&1
if errorlevel 1 goto one_failed
echo [one] OK
goto summary
:one_failed
set RC=1
echo [one] FAILED
goto summary

:summary
echo ================== summary ==================
dir obj\*.obj | find "file(s)"
dir obj\*.obj | find "file(s)" >> %LOG%
rem The verdict the host build loop reads.  Written by the batch rather than
rem inferred from the shell, because this driver carries on past a failing
rem compile, so the shell would report only the last file's status and call the
rem build good.
echo ATUI-BUILD-RC=%RC%
echo ATUI-BUILD-RC=%RC% >> %LOG%
