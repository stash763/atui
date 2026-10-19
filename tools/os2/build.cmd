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
rem                           others, or on a link that does not exist yet.
rem
rem  Why a batch file and not a command line: this target's command processor
rem  silently discards an input line longer than about 315 characters and says
rem  nothing at all.  A compile command carrying these switches is longer than
rem  that, so the loop lives here.
rem
rem  Why a loop over each directory and not a list of objects: the tree is 236
rem  files, two thirds of them tvision's, and a list would drift from the
rem  sources it describes.  "Everything in this directory" is what is meant, so
rem  "everything in this directory" is what is written.  Object names cannot
rem  collide -- checked -- and the sources are 8.3-clean upstream.
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
for %%f in (tvision\source\tvision\*.cpp) do if not "%%f"=="tvision\source\tvision\geninc.cpp" (echo [tv] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\ %%f >> %LOG% 2>&1 || set RC=1)

if "%1"=="tvision" goto summary

:atui
echo ================== atui ==================
for %%f in (src\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\ %%f >> %LOG% 2>&1 || set RC=1)
for %%f in (src\core\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\ %%f >> %LOG% 2>&1 || set RC=1)
rem The curl-backed transport is not part of this build: there is no libcurl
rem for Open Watcom.  That mirrors -DATUI_HAVE_NETWORK=OFF in CMake, which is
rem the configuration the requirements describe as a test harness -- so this
rem build must not advertise agent mode either, which is what that option
rem already arranges.  The socket transport that replaces it (Watcom has BSD
rem sockets and tcpip32.lib) is port work still to do.
for %%f in (src\net\provider.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\ %%f >> %LOG% 2>&1 || set RC=1)
for %%f in (src\settings\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\ %%f >> %LOG% 2>&1 || set RC=1)
for %%f in (src\tools\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\ %%f >> %LOG% 2>&1 || set RC=1)
for %%f in (src\tui\*.cpp) do (echo [at] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\ %%f >> %LOG% 2>&1 || set RC=1)

:one
rem One file, with the environment set up above.  The switches come from
rem defs.rsp, which is what keeps this command line inside the length the
rem command processor is willing to read at all.  Control jumps to labels rather
rem than using a parenthesised block, because `if errorlevel` reads whatever the
rem last command left behind and a block invites questions about which command
rem that was.
echo [one] %2
echo ---- %2 ---- >> %LOG%
%WPP% -fo=obj\ %2 >> %LOG% 2>&1
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
