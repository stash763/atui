// Definitions for the core AI stub (M1).
#include "ai.h"

namespace atui {
namespace core {

String ChatModelStub::ask( const String &userMessage, const String &history )
{
    ( void )history;
    return userMessage;
}

} // namespace atui::core
} // namespace atui
