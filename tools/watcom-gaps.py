#!/usr/bin/env python3
"""Where atui's sources use something Open Watcom 1.9 will not accept.

Two lists, both from measurements on the target, applied as patterns:

  * language syntax the compiler does not have -- lambdas, auto, range-for,
    default member initializers, list-initialisation, nullptr, override;
  * library names its C++ library does not provide -- std::to_string,
    std::getline, std::ostringstream (<sstream> is an empty stub), std::queue,
    std::shared_ptr, front()/back() on std::string, the three-argument
    std::string::compare, and long long where it is signed.

Why patterns and not a compile: the local C++98 gate (tools/gate98.py) checks the
language, but the library it compiles against is not the one on the target --
Apple's std::string has front() and back() in every dialect, so no compiler here
can tell you they are missing there.  The box reports those precisely and stops at
twenty errors a file, which is why this list exists next to it rather than in
place of it.

Several of these patterns need a human on the result -- vector::back() is C++98 and
string::back() is not, and no grep can tell them apart -- so a `?` marks the ones
that are not definite on their own.  Three classes are left out because they are
already settled: a name that a header in compat/os2/include provides, a match
inside a string literal (syntax.cpp keeps a list of C++ keywords as data), and a
file the OS/2 build does not compile, which is the curl-backed transport; those are
under `--deferred` until the socket layer replaces it.  With that done the list is
a work queue rather than a wall of text, which is what it is for: it is what a
round trip to the box would say, without the round trip.

Run from the repository root: `tools/watcom-gaps.py [path fragment ...]`.
"""
import argparse
import collections
import os
import re
import sys

# (pattern, why it fails on the target, whether every hit is definitely a problem)
PATTERNS = [
    (r"\[[^\]]*\]\s*\([^)]*\)\s*(?:const\s*)?\{", "lambda", False),
    (r"\bauto\s+[A-Za-z_]",                       "auto", True),
    (r"\bfor\s*\([^;{]*\s[A-Za-z_]\w*\s*:(?!:)",  "range-for", False),
    (r"\bnullptr\b",                              "nullptr", True),
    (r"\boverride\b",                             "override", False),
    # tvision's ttypes.h #defines noexcept/constexpr away when __cplusplus is
    # below 201103L, which is exactly Watcom's case, so these are fine in a file
    # that includes a tvision header -- and not fine anywhere else.  Hence "?".
    (r"\bnoexcept\b",                             "noexcept", False),
    (r"=\s*default\b",                            "= default", True),
    (r"=\s*delete\b",                             "= delete", True),
    (r"\bstatic_assert\b",                        "static_assert", True),
    (r"\busings?\s+[A-Za-z_][\w:]*\s*=",          "using X =", True),
    (r"std::to_string",                           "std::to_string", True),
    (r"std::sto[ildf]",                           "std::stoX", True),
    (r"std::getline",                             "std::getline", True),
    (r"std::[io]?ostringstream",                  "std::ostringstream", True),
    (r"std::[io]?stringstream",                   "std::stringstream", True),
    (r"std::shared_ptr",                          "std::shared_ptr", True),
    (r"std::make_shared",                         "std::make_shared", True),
    (r"std::unique_ptr",                          "std::unique_ptr", True),
    (r"std::function",                            "std::function", True),
    (r"std::queue",                               "std::queue", True),
    (r"std::unordered_(map|set)",                 "std::unordered_*", True),
    (r"\.front\(\s*\)",                           "front()", False),
    (r"\.back\(\s*\)",                            "back()", False),
    (r"\.data\(\s*\)",                            "data()", False),
    (r"\.compare\s*\([^)]*,[^)]*,",               "compare(pos,len,...)", True),
    (r"\bemplace_back\b|\bemplace\s*\(",          "emplace", True),
    (r"\bshrink_to_fit\b",                        "shrink_to_fit", True),
    (r"\bstd::(mutex|lock_guard|unique_lock|condition_variable|atomic|thread|chrono)\b",
     "needs compat/<...>", False),
    (r"#include\s*<(mutex|thread|atomic|chrono|condition_variable|future|dirent\.h|sys/wait\.h)>",
     "needs compat/<...>", False),
]

COMMENT = re.compile(r"^\s*(//|\*|/\*)")

# Files the OS/2 build does not compile, so a hit here is a note about the
# transport that is still to be ported rather than a gap in what builds.  The
# authority for this list is build.cmd's `atui` mode: everything under src/ except
# the curl-backed net files, of which only provider.cpp is compiled.  Reported in a
# section of its own, and it does not decide the exit status.
DEFERRED_DIRS = ("src/net/",)
DEFERRED_KEEP = ("src/net/provider.cpp",)

# Provided by compat/os2/include (measured on the target), so the hit is a
# reference to something that is there.
COMPAT_PROVIDED = "needs compat/<...>"

parser = argparse.ArgumentParser()
parser.add_argument("fragments", nargs="*")
parser.add_argument("--all", action="store_true", help="scan tests/ too")
parser.add_argument("--deferred", action="store_true",
                    help="show the not-compiled-on-the-target files too")
args = parser.parse_args()

dirs = ["src", "tests"] if args.all else ["src"]
files = []
for d in dirs:
    for dirpath, _, fs in os.walk(os.path.join(os.getcwd(), d)):
        for f in fs:
            if f.endswith((".cpp", ".h")):
                files.append(os.path.relpath(os.path.join(dirpath, f), os.getcwd()))
files.sort()
if args.fragments:
    files = [f for f in files if any(x in f for x in args.fragments)]


def deferred(f):
    return f.startswith(DEFERRED_DIRS) and f not in DEFERRED_KEEP


def string_spans(code):
    """(start, end) of every quoted run in the line, so a keyword mentioned in a
    string literal -- syntax.cpp keeps a list of C++ keywords as data -- is not
    reported as code that uses it."""
    spans = []
    i, n = 0, len(code)
    while i < n:
        if code[i] in "\"'":
            quote, start = code[i], i
            i += 1
            while i < n:
                if code[i] == "\\":
                    i += 2
                    continue
                if code[i] == quote:
                    spans.append((start, i))
                    break
                i += 1
            else:
                spans.append((start, n))
                break
        i += 1
    return spans


def in_string(spans, at):
    return any(a <= at <= b for a, b in spans)


tally = collections.Counter()
hits = collections.defaultdict(list)
dtally = collections.Counter()
dhits = collections.defaultdict(list)
for f in files:
    for n, line in enumerate(open(f, errors="replace"), 1):
        if COMMENT.match(line):
            continue
        code = line.split("//")[0]
        spans = None
        for pattern, why, definite in PATTERNS:
            m = re.search(pattern, code)
            if not m:
                continue
            if spans is None:
                spans = string_spans(code)
            if in_string(spans, m.start()):
                continue
            # compat headers are a solved problem, and counting them with the
            # gaps is what made 70 lines of noise out of this list once.
            if why == COMPAT_PROVIDED:
                continue
            mark = "" if definite else "   ?"
            (dhits if deferred(f) else hits)[f].append((n, why, code.strip()[:110], mark))
            (dtally if deferred(f) else tally)[why] += 1

for f in sorted(hits):
    print(f)
    for n, why, text, mark in hits[f]:
        print("    %4d  %-24s %s%s" % (n, why, text, mark))
print()
for why, n in tally.most_common():
    print("%5d  %s" % (n, why))
print("== %d hits in %d files ==" % (sum(tally.values()), len(hits)))

if args.deferred and dhits:
    print()
    print("-- not compiled by the OS/2 build (the transport to be ported) --")
    for f in sorted(dhits):
        print(f)
        for n, why, text, mark in dhits[f]:
            print("    %4d  %-24s %s%s" % (n, why, text, mark))
    print("== %d deferred hits in %d files ==" % (sum(dtally.values()), len(dhits)))

sys.exit(1 if tally else 0)
