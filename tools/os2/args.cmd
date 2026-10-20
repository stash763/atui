@echo off
rem args.cmd -- does this command processor pass the third argument?
rem
rem Asked because `build.cmd netlink probe_net.cpp probe_net.exe` behaved as if
rem %3 were empty: the section's `echo [netlink] %2 -> %3` expanded to a `>` with
rem nothing after it, created a 27-byte file named probe_net.exe containing that
rem echo, and the "successful link" that followed was a fiction of mine.  So the
rem question is whether the arguments arrive at all, and this answers it with
rem nothing else in the way.
echo A=[%1] B=[%2] C=[%3] D=[%4]
echo count: 1=%1 2=%2 3=%3
