@echo off
REM Run the self-test suite, which is the part of atui that CAN be exercised
REM down an SSH session.
REM
REM --self-test needs no console, so it answers for the whole binary without a
REM screen.  The TUI itself cannot be run this way -- an SSH session on Windows
REM gives a TUI no usable console -- and is driven on the desktop through
REM windows-mcp instead.  See WINDOWS_PORT.md, "Transport".
REM
REM The flag takes no arguments: main.cpp compares argv[1] against "--self-test"
REM exactly and runs the whole suite, so there is nothing to pass through.  A
REM build configured with -DATUI_BUILD_TESTS=OFF says so and returns 2.

setlocal
call "%~dp0env.bat" || exit /b 1

REM Where the binary landed depends on the generator: Ninja puts it straight in
REM the build directory, the MSBuild generator puts it under the configuration.
set "EXE=%ATUI_BUILD%\atui.exe"
if exist "%EXE%" goto found
set "EXE=%ATUI_BUILD%\Debug\atui.exe"
if exist "%EXE%" goto found
set "EXE=%ATUI_BUILD%\Release\atui.exe"
if exist "%EXE%" goto found
echo selftest: no atui.exe under "%ATUI_BUILD%" -- run tools\win\build.bat first 1>&2
exit /b 1

:found
echo selftest: "%EXE%"
"%EXE%" --self-test
exit /b %ERRORLEVEL%
