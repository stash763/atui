// Color theme selection for atui.
//
// Two schemes are supported for color screens:
//   - ThemeClassic: the standard Turbo Vision 2.0 palette (cpAppColor).
//   - ThemeDark:    a dark-mode variant derived from the classic palette,
//                   with dark backgrounds and readable light foregrounds.
//
// The initial scheme comes from the ATUI_THEME environment variable
// ("classic" or "dark"; anything else selects classic) and can be switched
// at runtime via setTheme().
//
// Both themes are answered for in either of the two color models Turbo Vision
// offers. A build with the legacy model -- where a TColorAttr is one BIOS
// attribute byte, with no TColor and no style bits, which is what Borland and
// Open Watcom give us on OS/2 and DOS -- cannot express the dark scheme as
// shades, so there it is a sixteen-color theme: black and dark-gray
// backgrounds, bright foregrounds. Views ask for themePalette() and roleAttr()
// and never have to know which model they are in.
#ifndef ATUI_TUI_THEME_H
#define ATUI_TUI_THEME_H

#define Uses_TPalette
#include <tvision/tv.h>

#include "md_style.h"

namespace atui {
namespace tui {

enum Theme { ThemeClassic, ThemeDark };

Theme currentTheme();
const char *themeName( Theme theme );
bool setTheme( Theme theme );

// The application palette matching the current theme (color mode only).
TPalette& themePalette();

// The attribute for one transcript role in the current theme. Views call this
// from mapColor(); it is the only place a role turns into a color.
TColorAttr roleAttr( Role role );

// Tell the theme what the screen can show, once, before the interface is built.
// `screenMode` is what THardwareInfo::getScreenMode() reports.
//
// The dark theme is designed in RGB colors and kept, for the screens that
// cannot show them, in the sixteen colors every console has. Which of the two
// is right is a property of the screen rather than of the compiler: a C++11
// compiler will happily build the RGB table for a console that has to quantize
// every one of its colors down to sixteen -- which is what a Windows console
// does until it is given ANSI color processing, and what an "ansi" terminal
// does everywhere. A theme that is never told keeps the RGB colors, as it did
// before the question was asked.
void setDisplayColorMode( unsigned short screenMode );

} // namespace atui::tui
} // namespace atui

#endif // ATUI_TUI_THEME_H