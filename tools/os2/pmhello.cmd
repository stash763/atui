@echo off
rem THIS BATCH NEVER RUNS pmhello.exe.  A PM program started from a telnet
rem session has no screen and hangs that session (and piles up when retried
rem -- that is what required the reboot).  pmhello.exe is started by hand at
rem the box's own display; it writes pmhello.run.txt as it goes.
rem
rem pmhello.cmd -- build the PM scaffold probe the way both references do:
rem compile, link with `system os2v2_pm`, then append the resource segment
rem (Watcom's own pmtest: rc -p -x hello.res hello.exe; without it
rem WinCreateStdWindow fails PMERR_RESOURCE_NOT_FOUND, measured 2026-10-27).
rem Results go to pmhello.out, which the host reads over FTP.
rem
rem Throwaway by design: the answer belongs in TESTING.md, the files go away.
d:
cd \atui
if not exist obj\pm mkdir obj\pm
if exist pmhello.exe del pmhello.exe
if exist pmhello.res del pmhello.res
if exist pmhello.run.txt del pmhello.run.txt

set RCPROG=d:\IBMDev\DCOS2012003\TOOLKITS\WARPTLKT\TOOLKIT\BIN\RC.EXE

echo === compile === > pmhello.out
wpp386 @defs.rsp -fo=obj\pm\ pmhello.cpp >> pmhello.out 2>&1
if errorlevel 1 goto done

echo === link === >> pmhello.out
if exist pmhello.rsp del pmhello.rsp
echo system os2v2_pm > pmhello.rsp
echo name pmhello.exe >> pmhello.rsp
echo op m >> pmhello.rsp
echo option stack=256k >> pmhello.rsp
echo file obj\pm\pmhello.obj >> pmhello.rsp
echo library os2386.lib >> pmhello.rsp
wlink @pmhello.rsp >> pmhello.out 2>&1
if errorlevel 1 goto done

echo === resources === >> pmhello.out
%RCPROG% -n -p pmhello.rc pmhello.exe >> pmhello.out 2>&1
if errorlevel 1 goto done

echo === linked, NOT run: start pmhello.exe at the box display === >> pmhello.out

:done
echo === end === >> pmhello.out
