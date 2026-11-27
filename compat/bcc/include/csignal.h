/* csignal -- <signal.h> in namespace std.

   Two names, both measured publishable: signal() and raise(), plus
   sig_atomic_t.  The signal numbers arrive with it; SIGINT and friends are
   macros in this CRT, so there is nothing to publish and no using-declaration
   that would work.

   One thing to check on the machines themselves rather than assume, and it is
   here so it does not get assumed: core/signals.cpp installs a SIGINT handler,
   and on Windows a console program is not signalled in the Unix sense -- Ctrl-C
   goes to a handler registered with SetConsoleCtrlHandler, and Borland's CRT
   bridges that to SIGINT on its own terms.  Whether atui's flag-setting handler
   is reached on 98, NT4 and 2000 is a measurement on those machines, not a fact
   about this header.  It has not been made yet, and the contract worth keeping
   in mind while it has not is the existing one: the handler writes one int and
   nothing else, which is as much as any of those bridges can be expected to
   allow.
*/
#ifndef ATUI_BCC_COMPAT_CSIGNAL_H
#define ATUI_BCC_COMPAT_CSIGNAL_H

#include <signal.h>

namespace std {

using ::raise;
using ::signal;
using ::sig_atomic_t;

} // namespace std

#endif /* ATUI_BCC_COMPAT_CSIGNAL_H */
