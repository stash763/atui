@echo off
REM Build atui for Windows.  Run tools\win\build.bat [-j N] [--release]
REM
REM --release must match what configure.bat was last given; with the MSBuild
REM generator the configuration is chosen per build rather than per directory,
REM and passing the wrong one builds the other binary in the same tree.  Ninja
REM is single-configuration and ignores --config, which is why it can be passed
REM unconditionally.

setlocal
call "%~dp0env.bat" || exit /b 1

set "BT=Debug"
set "JOBS="
set "KEEP="
:args
if "%~1"=="" goto doneargs
if /i "%~1"=="--release" set "BT=Release"
if /i "%~1"=="--keep" set "KEEP=-- -k 0"
if /i "%~1"=="-j" set "JOBS=-j %~2"
shift
goto args
:doneargs

if not exist "%ATUI_BUILD%\CMakeCache.txt" (
    echo build: "%ATUI_BUILD%" is not configured -- run tools\win\configure.bat first 1>&2
    exit /b 1
)

REM Two cores and 3.5 GB.  Ninja would run four cl.exe processes by default.
REM
REM --keep asks Ninja to carry on after the first failure ("-k 0"), which is what
REM you want when a tree is meeting MSVC for the first time: one pass that lists
REM every error beats twenty that each report the first one.  Everything after
REM "--" goes to the native tool, not to CMake.
"%CMAKE%" --build "%ATUI_BUILD%" --config %BT% %JOBS% %KEEP%
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="0" (echo build: OK) else (echo build: FAILED rc=%RC%)
exit /b %RC%
