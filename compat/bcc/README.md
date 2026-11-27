# Headers this compiler does not have

Everything here exists because Borland C++ 5.02 has no version of it. That is the
whole rule, and it is written down because the same rule, applied to the OS/2
build, has already been got wrong once (see `compat/os2/include/README.md`, whose
reasoning about namespace `std` and about adding to it applies here unchanged and
is not repeated).

The headers this installation cannot open, from `src/` and from tvision's classic
layer, measured by compiling one include per file:

    <cstddef> <cstdio> <cstdlib> <cctype> <ctime> <cerrno> <csignal> <cstdarg>
    <fstream> <istream> <mutex> <condition_variable> <chrono>

and one that opens and declares nothing:

    <cstring>   -- cstring.h is "C/C++ Run Time Library - Version 8.0" and is a
                   stub; std::strlen through it is "Qualifier 'std' is not a
                   class or namespace name"

`<ostream>`, `<sstream>`, `<streambuf>`, `<ios>`, `<iosfwd>`, `<span>`,
`<string_view>`, `<typeindex>` and `<unordered_map>` are also missing and are
also not here, because nothing the era build compiles asks for them: they are
reached only through tvision's modern terminal layer, and an era build does not
build that layer (see WINDOWS_PORT.md and `tvision/source/tvision/hardwrvr.cpp`,
which is a complete Win32 console driver in the classic layer and compiles under
this compiler unmodified).

## Two rules about names, both measured, neither visible from an error message

**An include with no extension is opened as eight characters plus `.h`.**
`#include <cstdio>` opens `cstdio.h`, and `#include <condition_variable>` opens
`conditio.h`. That is why every file in this directory ends in `.h` -- Borland
does not open an extensionless header at all, whatever the directory holds -- and
it is why `conditio.h` is spelled like that: a file named `condition_variable.h`
on the same include path is simply not found, and the compiler reports the name
it looked for, which is how it was worked out. A header added here with a name
longer than eight characters has to be given the eight-character name, or it sits
unread and the error appears somewhere far away.

**This directory is searched first and there is no fallback.** For `<fstream>`
that is a problem rather than a convenience: Borland owns `fstream.h`, the real
one is needed for the classes, and `#include <fstream.h>` from a compat
`fstream.h` opens the compat `fstream.h`. The real header can only be reached by
its own path, and BC5 does expand a macro in an include directive, so the build
driver writes one header containing that path:

    #define ATUI_BCC_INCLUDE_FSTREAM "C:/BC5/INCLUDE/fstream.h"

It is generated rather than passed with `-D` because the quoting does not survive
a bc5 response file (`-DATUI_X="C:/path"` on the command line works; the same
line in `@args.rsp` is "Bad file name format in include directive"), and an era
build passes its flags in a response file.

## What this compiler is, measured

Not a list of what to expect -- the diagnostics that produced these are in the
headers that respond to them.

* `__BORLANDC__` is 0x0520 and `__cplusplus` is **1**, so every
  `#if __cplusplus >= 201103L` in the tree takes its era branch by itself, and
  the era branches tvision already has engage without being told.
* `__FLAT__`, `__WIN32__` and `_Windows` are predefined. `WINVER` is 0x0400 and
  `_WIN32_WINNT` is not defined, so the newer console API is not even declared.
* Exceptions compile and work, with no special flags: `throw` and `catch` run and
  the object arrives intact.
* Free function templates work. **Member templates do not exist**, in any class,
  in any form -- which is the one constraint that changed the shape of something
  outside this directory, and `conditio.h` says what it cost.
* Namespaces, `bool`, RTTI (`typeid`, `dynamic_cast`), nested class templates,
  `typename T::X`, default arguments: present. Partial specialisation and
  `nullptr`: not.
* `long long` is not a type and `1000LL` is not a literal. `__int64` and `I64`
  are, and they are the same arithmetic -- measured through `%I64u` on an
  eighteen-digit value. `src/core/int64.h` is the seam.
* The standard library is Rogue Wave, in namespace std: `string`, `vector`,
  `map`, `set`, `list`, `deque`, `stack`, `queue`, `bitset`, `memory`, `utility`,
  `iterator`, `algorithm`, `limits`, `functional`'s adaptors. It has no streams
  anywhere near it -- the streams are the classic ones in the global namespace,
  and there is no `std::ostream` to be had from any header in the installation.
  `std::map` needs its comparator spelled out because Rogue Wave disables default
  template arguments for this compiler, in `stdcomp.h`.
* Rogue Wave's `string` has no `clear()`, no `front()`/`back()`, no
  `push_back()`, and its `erase()` takes iterators rather than positions. It does
  have `compare()` and `substr()`, which Watcom's lacks, so the two era targets
  are not missing the same things and the helpers in `core/strutil.h` say which
  gap each one fills.
* `snprintf` does not exist, in any spelling, anywhere in the include tree. The
  CRT's only formatter is `vsprintf`, which takes no length. `<cstdio>` here
  implements the bounded one and explains how it keeps it bounded.
* `ctype.h` declares the classifications twice, as functions and as macros, and
  the macros win: a using-declaration cannot name one, which is why `<cctype>`
  undefines them first and what that costs is written there.

## The language corners that changed source, measured one case at a time

Each of these is a compile of a small file, and the file is checked against
`g++ -std=c++98` first: an error from a program that is not a program measures
nothing, and one round of this work recorded a "compiler rule" that was a missing
brace in the probe. The table is 60 cases and every one of them agrees with what
is written here.

* **A function template may not return a pointer to a dependent type.** Declaring
  the result `const typename M::mapped_type *` stops the compiler outright --
  "Fatal ...: Internal compiler error" -- for every instantiation, including a
  body of `return 0;`, and including a map whose mapped type is `int`. A pointer
  to `typename M::key_type` is fine, and so is returning the same address as
  `const void *`, which pins it to the return type rather than to the loop or the
  map. Deducing the pointee from a null-pointer argument makes the return type
  non-dependent and compiles, so that is what `core/lookup.h` does.
* **A qualified-id with explicit template arguments cannot be parsed**
  (`n::f<int>( x )` is "Expression syntax"), and a template parameter that appears
  only in the return type cannot be reached at all, with or without a qualifier.
  Unqualified `f<int>( x )` and plain deduction are both fine, and instantiating
  one template for two types in one translation unit is fine.
* **Conditional expressions need more help than the standard says they should.**
  Two temporaries will not bind a `const` reference parameter ("Reference
  initialized with ..., needs lvalue of type the same") -- give the value a name.
  A `const char *` arm and a `std::string` arm will not unify ("Two operands must
  evaluate to the same type") -- `.c_str()` on the string arm makes them one type.
  Two literals are fine, and so is a named `const std::string` initialised from
  the conditional.
* **`std::map`'s iterators are dereferenced, not arrowed**: `it->first` is
  "Pointer to structure required on left side of -> or ->*" while `(*it).first`
  compiles; `std::vector`'s and `std::string`'s iterators take `->`. And the
  comparator has to be spelled out everywhere, including on a plain object:
  `std::map<std::string,int> m;` is "Too few arguments in template class name
  'map'", because Rogue Wave turns default template arguments off for this
  compiler in `stdcomp.h`.
* **A reference member needs a constructor.** `struct R { std::string &s; };` is
  "Reference member 'R::s' in class without constructors", and it still is when
  the class has member functions: aggregate initialisation from the caller is not
  the substitute.
* **The body of a class defined inside a function cannot see the namespace around
  it**: constructing a class type from the enclosing namespace is "Call to
  undefined function 'Span'", and using a sibling namespace as a qualifier is
  "Qualifier 'core' is not a class or namespace name". Member *declarations*
  resolve, a local class whose bodies touch only their own members is fine, and
  fully qualifying every name in the body does compile -- `src/tui/markdown.cpp`
  hoists its functor instead, which is the same argument written shorter.
* **A second anonymous namespace inside a named one is refused** ("Multiple
  declaration for 'x::<unnamedNS>'"; two at global scope are accepted). The files
  that need two sections say `namespace <filestem>_local { ... }` and reopen the
  scope with a using-directive, which is what an anonymous namespace means.

## What the operating system headers do and do not have

For the era build targets Windows 98 and NT 4, so the headers are as old as the
systems and the API gaps are the truth rather than a configuration mistake.

* No job objects (`CreateJobObjectA` is "Call to undefined function", with
  `<windows.h>` and without it, and `WIN32_LEAN_AND_MEAN` is a name this
  `windows.h` does not know), no `GetFinalPathNameByHandleA` (Vista), no
  `addrinfo`. `src/tools/run_command_tool.cpp` and `src/core/paths.cpp` say what
  each fallback costs the program.
* **`<winsock2.h>` before `<string>`**, or before anything that pulls in
  `windef.h`: the other order is "Multiple declaration for 'fd_set'". There is no
  `UINT_PTR` and no `uintptr_t` anywhere; `std::size_t` holds a pointer-sized
  value on every target atui builds for, which is why the code names that.
* `sys/stat.h` keeps `struct _stat`, `::_stat` and `_S_IFDIR` behind `__MSC`, so
  the era build uses the unsuffixed `struct stat`, `::stat` and `S_IFDIR`.
* The C library has the POSIX names and not the MS ones: `access`, `chdir`,
  `getcwd`, `putenv`, `isatty` compile and `_access`, `_chdir`, `_getcwd`,
  `_putenv`, `_isatty`, `commit` and `_commit` are each "Call to undefined
  function". `mkdir` and `_mkdir` are both there. There is no `unistd.h` at all.

## The linked image, and the one import that would not have started

Compiling 246 files is not the end of the target: the link asked two more
questions and each has an answer now.

* Two unresolved externals, `THardwareInfo::windowed()` and
  `THardwareInfo::redrawScreen()`: the era front never defined them because
  nothing in tvision calls them on this path, and atui does. Both are in
  `hardwrvr.cpp`, with why each answer is what it is.
* One import that is Windows 2000: `GetNumberOfConsoleMouseButtons` appeared in
  the image's KERNEL32 imports, from an inline in `hardware.h`, and Borland's own
  1997 import library carries a stub for it -- so it compiles, it links, and the
  loader stops the program before `main()` on Windows 98 and NT 4 with an
  entry-point-not-found message. It is resolved at run time now. That is the class
  of bug this section exists for, and the reason the import table is read rather
  than trusted: `tdump atui_era.exe` is the measurement, and the names after it are
  KERNEL32 (108), WS2_32 (14) and USER32 (2).

Anything else in that list that looked late was checked against the compiler's own
runtime: a program of one `main` that prints a line needs 43 KERNEL32 names, and
`GetStringTypeW`, `DeleteFileW` and the rest are in that baseline -- Borland's CRT
imports them for ordinary file I/O, which is the strongest evidence available here
that the era's `kernel32` exports them. `ws2_32.dll` ships with Windows 98 and, on
NT 4, with the Winsock 2 update: a deployment fact about a network program, not a
bug in one.

## snprintf, measured against the formatter it stands in for

`tools/bcc/snprintf_matrix.cpp` is one program built by g++ and by bcc32 -- 64
cases of flags, widths, precisions, `*` fields, 64-bit integers, truncations and
buffer sizes down to zero -- and each case reports the buffer, the return value,
and whether a canary around the buffer still says the formatter stayed inside the
size it was given. 58 agree character for character. The six that do not are the
three known deviations, named rather than summarised:

* the clamp: `%.2000s` gives 1024 characters under bcc32 and 2000 under glibc, and
  so do `%2000d` and `%1500.4f`;
* a `*` width above 4096 is refused, where glibc prints it;
* Borland's own arithmetic: `-0.0` prints as `0.000000` where glibc prints
  `-0.000000`, which is the CRT's 1997 idea of a sign and not something a wrapper
  can add back.

Writing it found three defects, all in `cstdio.h`, all worth listing because the
first was a buffer overflow in the code whose whole purpose is to prevent them:

* A `*` width or precision was clamped to `kMaxField` when sizing the temporary
  and then copied verbatim into the rewritten format, so the CRT used the
  caller's real value -- `%.*s` of 3000 writes 3000, measured -- into a buffer
  sized for 1024. The era binary came back with a page fault at `0x7878787C`:
  four `x` bytes, the string it had been formatting.
* A `*` was read from a `va_list` passed **by value** into the function that reads
  it, so the walk never moved past one; a second conversion in the same format was
  sized from the first conversion's argument. Measured separately: on this
  compiler `va_list walk = args` does copy, and `va_arg` does advance the copy.
* A `n == 0` call returned -1 where both CRTs return the length that would have
  been written -- a caller's `if (snprintf(...) < 0)` reading a good print as a
  failed one.

Two more things the matrix is only able to say because the host build is the
comparison: the 1997 `printf` has no `z` modifier and prints the rest of the
format string literally when it meets one, so `%zu` is safe through this header
and nowhere else; and a negative `*` precision means "as if absent", which for
`%s` means the whole string, so the room comes from the string.

## Checking changes here

`tools/bcc/bcc-run.sh` drives the compiler under wine against this tree, compiles
one file in well under a second, and has the installation's headers on disk to
grep. Wine runs the compiler faithfully; it is not a Windows 95 console, so what
this directory can prove is that the code compiles and that its objects link.
Anything about behaviour -- the console driver, Ctrl-C, the clock under a load --
is a measurement on the machines, and is written up as one or not at all.

A probe of this compiler lies in four ways, all of them silent, each of them
costing a round of believing something false:

* **Wrong prefix, no compiler, no error.** `WINEPREFIX` decides which prefix's
  `C:\BC5` runs; the default `~/.wine` has no Borland installation, so the
  compiler is never found, nothing prints an `Error` line, and a probe that
  believes "no error line means it compiles" reports success for every case.
  Every harness here sets the prefix itself (`tools/bcc/bcc-run.sh`, and
  `WINEPREFIX` in the environment otherwise).
* **A compile that did not happen looks like a compile that succeeded.** The
  positive check is the object file: no object and no error line is neither pass
  nor fail, it is "did not run", and `bcc-run.sh` treats it that way.
* **A probe with a syntax error of its own** reports a compiler rule that does not
  exist. So each case is compiled by `g++ -std=c++98` before it is believed from
  `bcc32`, and a case g++ refuses is reported as a broken case rather than as a
  result.
* **A control that does not fail voids the run.** One file that must not compile
  goes through the same call path as the cases (`bad.cpp` beside the probe
  driver); if it compiles, the driver stops rather than printing a table.

Two more, about reading what comes back: `bcc32` stops after 26 errors, so a file's
error count is a floor and not a measure; and `-e` takes its name glued to it
(`-efoo.exe`, since `-e foo.exe` asks the compiler to read a file called
`foo.exe`).
