#!/usr/bin/env python3
"""Compile atui's sources as C++98, the dialect Open Watcom has to accept.

The target compiler reports its own error list, but it stops at twenty errors per
file and the tree is 30 files, so a Watcom run is a slow way to enumerate a work
list.  clang will do the enumeration in seconds; the box then checks the answer.

Two known lies, both from tvision, both because Watcom defines __cplusplus as 1
while clang -std=c++98 defines it as 199711L:

  * tstrview.h's `return {str, len};` -- an error for clang, never seen by Watcom,
    whose __cplusplus keeps the whole std::string conversion out of the compile.
  * everything gated on that conversion, which clang therefore lets atui rely on
    and Watcom does not: an implicit TStringView <-> std::string.

Run from the repository root.  Exit status is non-zero when a source of ours has
an error, ignoring those two.  `python3 tools/gate98.py tui` narrows it to the
files whose path contains "tui".
"""
import collections
import os
import re
import subprocess
import sys

ROOT = os.getcwd()

DEFINES = ["ATUI_HAVE_NETWORK=1", "ATUI_SELFTEST=1",
           'ATUI_INSTALL_DIR="/usr/local/bin"',
           'ATUI_SOURCE_DIR="%s"' % ROOT, 'ATUI_VERSION="0.0.0-gate98"']

srcs = []
# tests/ is deliberately not here: the OS/2 and DOS builds do not compile the
# self-test suite -- 200 KB of test code is not something a 300 KB target ships --
# so its lambdas and mock handlers are allowed to stay C++14.  The program is not.
for d in ("src",):
    for dirpath, _, files in os.walk(os.path.join(ROOT, d)):
        for f in sorted(files):
            if f.endswith(".cpp"):
                srcs.append(os.path.relpath(os.path.join(dirpath, f), ROOT))
if len(sys.argv) > 1:
    srcs = [s for s in srcs if any(a in s for a in sys.argv[1:])]

args = ["clang++", "-fsyntax-only", "-std=c++98", "-Wall", "-Wextra",
        "-Wno-unused-parameter", "-ferror-limit=0"]
args += ["-D" + d for d in DEFINES]
args += ["-I", "src", "-I", "tests", "-I", "tvision/include"]

jobs = [(s, subprocess.Popen(args + [s], cwd=ROOT, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, universal_newlines=True))
        for s in srcs]

byline = collections.defaultdict(list)
counts = collections.Counter()
for s, p in jobs:
    for line in p.stdout:
        m = re.match(r"^(?P<f>[^\s:]+):(?P<n>\d+):\d+: (error|warning): (?P<m>.*)$", line.strip())
        if not m or m.group("f").startswith("tvision/"):
            continue
        byline[m.group("f")].append((int(m.group("n")), m.group("m")))

total = 0
for f in sorted(byline):
    items = sorted(set(byline[f]))
    print("%s: %d" % (f, len(items)))
    for n, msg in items[:40]:
        print("    %4d  %s" % (n, msg[:150]))
    total += len(items)
print("== %d diagnostics in %d files ==" % (total, len(byline)))
sys.exit(1 if total else 0)
