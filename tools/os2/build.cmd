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
set WPP=wpp386 @defs.rsp

set RC=0

if "%1"=="atui" goto atui

echo ================== tvision library ==================
for %%f in (tvision\source\tvision\*.cpp) do (echo [tv] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\ %%f >> %LOG% 2>&1 || set RC=1)

echo ================== tvision platform ==================
for %%f in (tvision\source\platform\*.cpp) do (echo [tv] %%f & echo ---- %%f ---- >> %LOG% & %WPP% -fo=obj\ %%f >> %LOG% 2>&1 || set RC=1)

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
