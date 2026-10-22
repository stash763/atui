// Bare atui TUI shell entry point.

#ifndef ATUI_TUI_APP_H
#define ATUI_TUI_APP_H

#include <string>

namespace atui {
namespace tui {

// Ask this as soon as the interface is up, as if it had been typed, and stay open for
// the answer and whatever comes after.  Two reasons it is part of the program rather
// than a comment in a test script: opening atui on a question is something a person
// wants (`atui --ask "why is the build red?"`), and a target whose terminal eats
// keystrokes -- measured on the OS/2 box, where a burst loses characters and Enter
// sometimes does not arrive at all -- cannot be tested through its prompt at all
// otherwise.  The turn takes the same path either way: the text goes into the prompt
// and `processMessage_()` is called, so nothing downstream can tell the difference.
void setStartupMessage( const std::string &text );

int run_app();

}
}

#endif // ATUI_TUI_APP_H
