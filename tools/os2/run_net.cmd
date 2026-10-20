@echo off
rem run_net.cmd -- run the socket probe with its output in a file, and a marker
rem on each side of it, because the two failure modes look the same from here:
rem a program that produces nothing, and a program that never started.  With a
rem line before and a line after, an empty middle or a missing last line says
rem which one it was.  Fetch pn.out; the run takes a few seconds.
d:
cd \atui
echo ---- start ---- > pn.out
probe_net.exe >> pn.out 2>&1
echo ---- rc=%errorlevel% ---- >> pn.out
