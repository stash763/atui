@echo off
rem Does a `for %%f in (dir\*.cpp)` fileset match anything on this command
rem processor, and does `call :label` reach a subroutine?  `netall` compiled zero
rem files while reporting OK, which is what one of those two answers explains, and
rem this box has already turned out to differ from cmd.exe about `%~4` (it expands
rem empty; use `%4`).  Each question gets its own echo so the answer is readable
rem from the console without a log.
echo PROBE start
echo PROBE cwd=%cd%
if exist src\net\http.cpp (echo PROBE src\net\http.cpp exists) else (echo PROBE src\net\http.cpp MISSING)
echo PROBE dir says:
dir src\net\*.cpp | find ".cpp"
echo PROBE loop over fileset:
for %%f in (src\net\*.cpp) do echo PROBE matched %%f
echo PROBE call test:
call :sub one two
echo PROBE after call, RC=%RC%
echo PROBE done
goto end

:sub
echo PROBE in :sub, arg1=%1 nx=%~n1
set RC=7
goto :eof

:end
