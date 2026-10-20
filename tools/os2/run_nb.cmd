@echo off
rem run_nb.cmd -- build and run the non-blocking probe in ONE session, with
rem everything in one file.  The box keeps telnet session slots after a
rem connection is gone, and a build-plus-run as two visits takes two of them for
rem what needs one.  Fetch nb.out afterwards.
d:
cd \atui
echo ---- build ---- > nb.out
build.cmd netlink probe_nb.cpp probe_nb.exe >> nb.out 2>&1
echo ---- run ---- >> nb.out
probe_nb.exe >> nb.out 2>&1
echo ---- done ---- >> nb.out
