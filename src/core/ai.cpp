// Definitions for the core AI stub (M1).
#include "ai.h"

namespace atui {
namespace core {

// Says there is nobody to ask, in as many words.
//
// atui needs a server: there is no offline mode, and a build without a transport is
// the test harness, not a smaller program.  This used to echo the user's message
// back, which was fine while the TUI was the thing under test and is not fine
// afterwards: an echo is indistinguishable from a model that answers in one word, so
// somebody who does not know which build they started concludes the program is
// broken instead of concluding what they have to do about it.
String ChatModelStub::ask( const String &userMessage, const String &history )
{
    ( void )userMessage;
    ( void )history;
    return String( "This build of atui has no network transport, so there is nobody "
                   "to ask. atui needs a server; the build without one is for running "
                   "the tests." );
}

} // namespace atui::core
} // namespace atui
