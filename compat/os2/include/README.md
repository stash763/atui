# Headers this compiler does not have

Anything in this directory is found by the OS/2 build's include path, and is
here because Open Watcom 1.9 has no version of it to find first. That last
clause is the whole rule, and it is what the previous contents of this
directory got wrong.

What belongs here is the list of headers atui includes that this installation
does not provide at all: `<dirent.h>`, `<sys/wait.h>`, `<mutex>`, `<thread>`,
`<atomic>`, `<chrono>`, `<condition_variable>`.

What does not belong here is forwarders for long-named standard headers. There
were seven — `<algorithm>` including `<algorith>`, and so on — written on the
theory that this installation had lost the long names of Watcom's extensionless
C++ headers. Measured on the target, that theory is wrong twice over:

* **The compiler resolves the long names by itself.** Its own header is named
  `algorith`, the first eight characters, and OS/2 resolves a name that does not
  exist by trying its truncated form, so `#include <algorithm>` opens
  `D:\WATCOM\h\algorith` with no help. A ten-character extensionless include in
  a directory of our own also opens correctly, so the compiler is not blind to
  long names — it is the installed file that is short, and that is fine.

* **Nothing here could shadow a header Watcom has.** `D:\WATCOM\h` is searched
  before the `INCLUDE` variable and before `-i=` directories, whatever order
  they are given in. `<streambuf>` went on reaching `D:\WATCOM\h\streambu` with
  our own forwarder sitting first on the include path, which is how this got
  noticed: the forwarder was correct, uploaded, and never opened.

## Two traps in the headers that *are* there

Watcom 1.9's C++ library has files that exist in order to mislead you. Each of
these was checked by compiling one name in one file, not by looking at the
directory:

* **`<sstream>` is empty.** Its entire content, after the guards, is
  `namespace std { }`. It opens, it compiles, and it declares nothing — so
  `std::ostringstream` and `std::istringstream` are unavailable and no header
  placed here can ever supply them, because their own `<sstream>` always wins
  the lookup. `<locale>`, `<queue>` and `<valarray>` are the same kind of stub.
  String formatting therefore has to go behind one of atui's own helpers rather
  than through `<sstream>`, which is also the smaller answer on every target.

* **`<unordered_map>` is only `#include <_hash.h>`** — 41 lines, and there is no
  `std::unordered_map`.

The useful part of the library is real and substantial: `string`, `vector`,
`map`, `list`, `deque`, `set`, `stack`, `fstream`, `iostream`, `iomanip`,
`algorithm` (via `_algsort.h` and friends), `iterator`, `numeric`, `utility`,
`limits`, `bitset`, `functional`'s old adaptors, and `strstream` — the
pre-standard string stream, which is what `sstream` was eventually replaced by
and which does work here.

C++11 is not: no `std::function` (only the pre-standard adaptors), no
`unique_ptr` (though `shared_ptr` and `auto_ptr` are there), no `to_string`, no
`string::front`/`back`, no `enable_if`.

And one library defect to design around rather than fight: `std::map` declares
`find()` only as a non-const member, so calling `find()` through a
`const std::map &` is a compile error — `count()` and `lower_bound()` do have
const forms.
