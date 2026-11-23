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
REM Say which binary is about to be run, and when it was built.  This line is not
REM decoration: a failed build leaves the previous atui.exe sitting exactly where
REM it was, and the suite then reports a clean pass about code that does not
REM compile.  That happened during the Windows port -- C2039 in a test file, a
REM build that failed, and three "passing" runs afterwards of a binary none of the
REM changes were in.  If the timestamp here is older than the build you just ran,
REM the results under it are about somebody else's tree.
for %%F in ("%EXE%") do echo selftest: %%~fF  built %%~tF
"%EXE%" --self-test
exit /b %ERRORLEVEL%
