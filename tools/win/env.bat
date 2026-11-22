@echo off
REM The MSVC environment for building atui on Windows.  Called by configure.bat
REM and build.bat, and useful on its own to get a shell holding cl, cmake and
REM ninja.  Paths are what this box has, written down rather than guessed; the
REM measurements behind them are in WINDOWS_PORT.md.
REM
REM Only the x86 environment is offered.  This box runs a 32-bit Windows 10, so
REM Hostx86 is the only compiler that can execute, and an x64 build could never
REM be run here.
REM
REM Deliberately no setlocal.  This script exists to change the caller's
REM environment, and vcvarsall's LIB and INCLUDE have to survive it: wrapping
REM the call in setlocal and re-exporting only PATH is enough for everything to
REM compile and then fail at the link with "cannot open file kernel32.lib",
REM which is how long this took to find.

set "VSROOT=C:\Program Files\Microsoft Visual Studio\2017\Community"
set "CMAKEROOT=C:\cmake"

set "VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" (
    echo env: no vcvarsall at "%VCVARS%" 1>&2
    exit /b 1
)
if not exist "%CMAKEROOT%\bin\cmake.exe" (
    echo env: no cmake at "%CMAKEROOT%\bin\cmake.exe" 1>&2
    exit /b 1
)

REM The source tree is the repo this script lives in.  Builds go outside it, so
REM that nothing a build produces can be mistaken for a source file.
for %%D in ("%~dp0..\..") do set "ATUI_ROOT=%%~fD"
if not defined ATUI_BUILD set "ATUI_BUILD=%ATUI_ROOT%-build"

call "%VCVARS%" x86 >"%TEMP%\atui-vcvars.log" 2>&1
if errorlevel 1 (
    echo env: vcvarsall x86 failed, see "%TEMP%\atui-vcvars.log" 1>&2
    type "%TEMP%\atui-vcvars.log" 1>&2
    exit /b 1
)

REM vcvars sets these; if they came through empty the environment is not usable
REM and every failure later would be about something else.  Checked once, here.
if not defined LIB (
    echo env: vcvarsall left LIB empty -- no include or library path 1>&2
    exit /b 1
)

REM C:\cmake comes first: its 3.31.12 is what WINDOWS_PORT.md measured, and the
REM 3.12 VS2017 bundles is too old for atui's 3.16 requirement.
REM
REM VS2017's bundled Ninja is NOT on this path, deliberately: it is a 64-bit
REM executable and this box runs a 32-bit Windows 10, so it cannot execute.
REM Ninja comes from the pip win32 wheel instead (py -m pip install ninja),
REM which installs a 32-bit one that does run.
set "PATH=%CMAKEROOT%\bin;C:\Python\scripts;%PATH%"

set "CMAKE=%CMAKEROOT%\bin\cmake.exe"
set "CTEST=%CMAKEROOT%\bin\ctest.exe"

REM Find a Ninja that executes, not merely one that exists, and hand CMake the
REM path to it.  Handing over no path lets CMake search PATH itself, and on this
REM box it finds VS2017's 64-bit copy and reports the one it cannot run as
REM "unknown error".
set "ATUI_NINJA="
for /f "delims=" %%I in ('where ninja 2^>nul') do if not defined ATUI_NINJA set "ATUI_NINJA=%%I"
if defined ATUI_NINJA (
    "%ATUI_NINJA%" --version >nul 2>&1
    if errorlevel 1 set "ATUI_NINJA="
)
