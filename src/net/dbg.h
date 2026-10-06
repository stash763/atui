// Debug file writer for the net module (gated on ATUI_DEBUG_KEYS).
#ifndef ATUI_NET_DBG_H
#define ATUI_NET_DBG_H

#include <cstdio>
#include <cstdarg>

namespace atui {
namespace net {

void dbg( const char *fmt, ... );

} // namespace atui::net
} // namespace atui

#endif
