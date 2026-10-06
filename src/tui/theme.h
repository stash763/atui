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
#ifndef ATUI_TUI_THEME_H
#define ATUI_TUI_THEME_H

#define Uses_TPalette
#include <tvision/tv.h>

namespace atui {
namespace tui {

enum Theme { ThemeClassic, ThemeDark };

Theme currentTheme();
const char *themeName( Theme theme );
bool setTheme( Theme theme );

// The application palette matching the current theme (color mode only).
TPalette& themePalette();

} // namespace atui::tui
} // namespace atui

#endif // ATUI_TUI_THEME_H