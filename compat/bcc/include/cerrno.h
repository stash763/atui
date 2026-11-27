/* cerrno -- the one header whose standard content is a macro.

   Borland has no <cerrno>, so the include fails; and in this CRT `errno` is a
   macro for an indirection through _errno(), which is exactly what the standard
   says <cerrno> is allowed to be: [cerrno] defines errno as a macro and does not
   promise std::errno.  So there is nothing to publish and no reason to write a
   using-declaration that would fail: this file's whole job is to exist, so that
   the include line in run_command_tool.cpp and thread_posix.cpp means what it
   says.
*/
#ifndef ATUI_BCC_COMPAT_CERRNO_H
#define ATUI_BCC_COMPAT_CERRNO_H

#include <errno.h>

#endif /* ATUI_BCC_COMPAT_CERRNO_H */
