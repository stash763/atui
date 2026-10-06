// AI backend interface for the portable core (C++98).
//
// The core keeps a minimal, C++98-safe interface (the M1 stub). The real
// streaming backend lives in the net module (C++14) and is not part of this
// interface so the core stays portable to Borland/Open Watcom.
#ifndef ATUI_CORE_AI_H
#define ATUI_CORE_AI_H

#include "core/string.h"

namespace atui {
namespace core {

// M0/M1 stub: returns the user message as the "reply" so the TUI flow can be
// verified without a backend.
class ChatModelStub
{
public:
    String ask( const String &userMessage, const String &history );
};

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_AI_H
