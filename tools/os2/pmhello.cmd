rem THIS BATCH NEVER RUNS pmhello.exe.  A PM program started from a telnet
rem session has no screen and hangs that session (and piles up when retried
rem -- that is what required the reboot).  pmhello.exe is started by hand at
rem the box's own display; it writes pmhello.run.txt as it goes.
rem
rem pmhello.cmd -- compile and link the PM scaffold probe (pmhello.cpp).
rem Results go to pmhello.out, which the host reads over FTP.
rem
rem Throwaway by design: the answer belongs in TESTING.md, the files go away.
d:
cd \atui
if not exist obj\pm mkdir obj\pm
if exist pmhello.exe del pmhello.exe
if exist pmhello.run.txt del pmhello.run.txt

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

echo === linked, NOT run: start pmhello.exe at the box display === >> pmhello.out

:done
echo === end === >> pmhello.out
