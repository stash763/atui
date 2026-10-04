// AI chat backend interface + M1 stub.
//
// C++98, no <string>, no exceptions: the core must compile on older
// compilers (Borland C++ 4.52 / Open Watcom) for the DOS build.
#ifndef ATUI_CORE_AI_H
#define ATUI_CORE_AI_H

#include "string.h"

namespace atui {
namespace core {

// Interface for an AI chat model.
class IChatModel {
public:
    virtual ~IChatModel();

    // Returns the assistant's reply to `userMessage`, given the prior
    // `history` transcript (already formatted). Synchronous.
    virtual String ask( const String& userMessage, const String& history ) = 0;
};

// A non-network stub: echoes the user's message so the TUI chat flow can be
// built and tested without a backend. M2 replaces this with a real
// OpenAI-compatible transport (libcurl + SSE, via ATUI_HAVE_NETWORK).
class ChatModelStub : public IChatModel {
public:
    String ask( const String& userMessage, const String& history );
};

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_AI_H
