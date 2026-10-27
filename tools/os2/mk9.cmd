@echo off
rem ====================================================================
rem  candidate9 in one command, for when the telnet server is jammed and
rem  the host cannot drive the build itself.
rem
rem  What candidate9 is: the flush-time scroll shortcut in the tvision
rem  driver (a scrolled transcript goes out as one VioScrollUp plus the
rem  new rows instead of one cell-write call per row -- local2.trc said
rem  the kernel display path was 43%% of the UI thread while the
rem  application itself was under 2%%), and the flush cost folded into
rem  the streaming repaint interval.
rem
rem  Run from any command window on this box:
rem
rem        d:\atui\mk9.cmd
rem
rem  It takes about five minutes, prints a line per step, and leaves the
rem  verdicts in d:\atui\mk9.done and each step's compiler log in
rem  mk9_<step>.log -- the host reads all of those over FTP, so nothing
rem  on the screen has to be transcribed.  Four rc=0 lines means go.
rem ====================================================================
d:
cd \atui
if exist mk9.done del mk9.done
if exist mk9_tvone.log del mk9_tvone.log
if exist mk9_lib.log del mk9_lib.log
if exist mk9_atui.log del mk9_atui.log
if exist mk9_exe.log del mk9_exe.log

echo [1/4] tvision driver (hardwvos2.cpp) ...
call build.cmd tvone tvision\source\tvision\hardwvos2.cpp
echo tvone rc=%RC%> mk9.done
copy /v build.log mk9_tvone.log >nul

echo [2/4] tvision library ...
call build.cmd lib
echo lib rc=%RC%>> mk9.done
copy /v build.log mk9_lib.log >nul

echo [3/4] atui sources ...
call build.cmd atui
echo atui rc=%RC%>> mk9.done
copy /v build.log mk9_atui.log >nul

echo [4/4] link candidate9.exe ...
call build.cmd exe candidate9.exe
echo exe rc=%RC%>> mk9.done
copy /v build.log mk9_exe.log >nul

echo.
echo ==================== mk9 finished ====================
dir candidate9.exe
type mk9.done
echo =======================================================
echo Ask the host: it can read d:\atui\mk9.done over FTP.
