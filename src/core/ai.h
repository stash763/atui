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

// What answers when this build has no transport: not a model, and not an echo of
// the question -- `ask()` says plainly that there is nobody to ask.  See ai.cpp.
// The TUI keeps working around it so the interface can be exercised headlessly.
class ChatModelStub
{
public:
    String ask( const String &userMessage, const String &history );
};

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_AI_H
