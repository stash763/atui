// Definitions for the AI chat backend interface + M1 stub.
#include "ai.h"

namespace atui {
namespace core {

IChatModel::~IChatModel() {}

String ChatModelStub::ask( const String& userMessage, const String& history )
{
    (void)history;
    String out;
    out += userMessage;
    return out;
}

} // namespace atui::core
} // namespace atui
