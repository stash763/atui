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

} // namespace atui::tui
} // namespace atui

#endif // ATUI_TUI_THEME_H