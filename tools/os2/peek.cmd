@echo off
rem peek.cmd -- a directory listing, written to a file.
rem
rem Header archaeology needs listings, and two of the ways to get one are
rem unreliable here: the telnet console cuts wide output, and `atui-remote
rem --exec` intermittently reports that a command produced no output at all.
rem Writing to a file and fetching it over FTP works every time, which is the
rem only property worth having.  Run it, then `atui-remote get peek.txt`.
rem
rem What it asks is where the BSD socket headers actually live: <sys/socket.h>
rem resolves for a compile -- probe_sock.c built and spoke HTTP with it -- but
rem `dir d:\watcom\h\os2\sys\socket.h` says no such file, and those two cannot
rem both be true unless the file is somewhere else and named differently.

d:
cd \atui
dir d:\watcom\h\os2\sys   > peek.txt
dir d:\watcom\h\sys      >> peek.txt
dir d:\watcom\h\socket.h >> peek.txt
dir d:\watcom\h\os2\tcp* >> peek.txt
echo done
