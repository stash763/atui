// Reading a text file one line at a time.
//
// Watcom has no free std::getline for a std::string (measured: "std::getline has
// not been declared").  Its istream::getline works on a fixed buffer instead,
// which is the wrong shape for a line of unknown length, so the line is
// assembled character by character.  atui reads small text files this way -- its
// own settings, an undo journal's .INF -- so the simplicity costs nothing.

#ifndef ATUI_CORE_LINES_H
#define ATUI_CORE_LINES_H

#include <istream>
#include <string>

namespace atui {
namespace core {

// Reads up to the next '\n' into `out`, without the end of line.  A '\r' in
// front of it is dropped: atui writes files with '\n' only, but a settings file
// edited by hand on OS/2 or DOS may not be, and a trailing '\r' in a value is
// the kind of bug that shows up as "the theme is called dark " and nothing else.
//
// Returns false when there was nothing more to read, which is how a caller
// writes `while( readLine( in, line ) )`.
bool readLine( std::istream &in, std::string &out );

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_LINES_H
