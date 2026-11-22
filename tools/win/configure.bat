@echo off
REM Configure the Windows build.  Run tools\win\configure.bat [--release] [--net] [--clean]
REM
REM Network is OFF unless --net says otherwise, which is phase P0/P1 of
REM WINDOWS_PORT.md: the sockets are a separate phase, and configuring without
REM them means the errors that show up are the ones about this tree meeting
REM MSVC for the first time rather than the ones about Winsock.
REM
REM The runtime library is left at the generator default for the same reason.
REM /MT is wanted in the end -- one self-contained binary, like the OS/2 build
REM ships -- but switching it on also switches tvision's TV_USE_STATIC_RTL, and
REM both changes landing before anything compiles is two ways to explain the
REM same failure.
REM
REM --clean throws the cache away first.  Needed whenever the generator or the
REM make program changes, because CMake keeps whatever it found last time.

setlocal
call "%~dp0env.bat" || exit /b 1

set "BT=Debug"
set "NET=OFF"
:args
if "%~1"=="" goto doneargs
if /i "%~1"=="--release" set "BT=Release"
if /i "%~1"=="--net" set "NET=ON"
if /i "%~1"=="--clean" set "CLEAN=1"
shift
goto args
:doneargs

if not "%CLEAN%"=="1" goto noclean
if exist "%ATUI_BUILD%\CMakeCache.txt" del /q "%ATUI_BUILD%\CMakeCache.txt"
if exist "%ATUI_BUILD%\CMakeFiles" rmdir /s /q "%ATUI_BUILD%\CMakeFiles"
:noclean

REM Ninja is preferred -- one build directory, one configuration, faster.  But
REM "preferred" has to be earned by asking, because on a 32-bit Windows only a
REM 32-bit ninja runs and the one VS2017 bundles is 64-bit; env.bat already ran
REM the candidates and only reports the ones that answered.
set "GEN=Ninja"
set "ARCH="
set "MAKEPRG="
if not defined ATUI_NINJA (
    set "GEN=Visual Studio 15 2017"
    set "ARCH=-A Win32"
    echo configure: no runnable Ninja, using the MSBuild generator instead
) else (
    set "MAKEPRG=-DCMAKE_MAKE_PROGRAM=%ATUI_NINJA%"
)

echo configure: "%ATUI_ROOT%"
echo        to: "%ATUI_BUILD%"
echo generator: %GEN%
echo      build: %BT%   network: %NET%
echo.

"%CMAKE%" -G "%GEN%" %ARCH% %MAKEPRG% -S "%ATUI_ROOT%" -B "%ATUI_BUILD%" ^
    -DCMAKE_BUILD_TYPE=%BT% ^
    -DATUI_HAVE_NETWORK=%NET% ^
    -DATUI_BUILD_TESTS=ON
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="0" (echo configure: OK) else (echo configure: FAILED rc=%RC%)
exit /b %RC%
