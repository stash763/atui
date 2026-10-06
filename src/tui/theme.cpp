// Color theme implementation. See theme.h.

#define Uses_TPalette
#define Uses_TProgram
#include <tvision/tv.h>

#include <cstddef>
#include <cstring>
#include <string>

#include "theme.h"

namespace atui {
namespace tui {

namespace {

Theme g_theme_ = ThemeDark;
bool g_themeInit_ = false;

// Case-insensitive "classic" match for the ATUI_THEME value. Dark is the
// default; set ATUI_THEME=classic to opt into the stock Turbo Vision palette.
bool isClassicEnv( const char *s )
{
    const char c[] = "classic";
    if( !s ) return false;
    for( int i = 0; i < 7; ++i )
    {
        char a = s[i];
        if( a == '\0' ) return false;
        if( a >= 'A' && a <= 'Z' ) a += 'a' - 'A';
        if( a != c[i] ) return false;
    }
    return s[7] == '\0';
}

void initTheme_()
{
    if( g_themeInit_ ) return;
    g_themeInit_ = true;
    if( isClassicEnv( getenv( "ATUI_THEME" ) ) )
        g_theme_ = ThemeClassic;
}

// Dark-theme color replacement tables, indexed by the 16 classic BIOS
// colors. Foreground colors become readable light shades; background
// colors become dark shades. Every classic palette entry keeps its
// fg/bg roles, so the whole Turbo Vision layout (desktop, menus,
// windows, dialogs, buttons) maps onto a coherent dark scheme.

const uint32_t fgDark[16] =        // classic fg -> dark-theme fg
{
    0xE8EAED, // 0  black        -> near-white
    0x7FA8DC, // 1  blue          -> light blue
    0x9BD49E, // 2  green         -> soft green
    0x74CFCF, // 3  cyan          -> soft cyan
    0xE89A9A, // 4  red           -> soft red
    0xDCA9DC, // 5  magenta       -> soft magenta
    0xE9CB94, // 6  brown         -> soft amber
    0xD6DAE0, // 7  light gray    -> light gray
    0xA9B6C3, // 8  dark gray     -> brightened gray (secondary text)
    0xB3D0F0, // 9  lt blue
    0xBFE4B8, // 10 lt green
    0xAEE4E0, // 11 lt cyan
    0xF2B4B0, // 12 lt red
    0xE6C4E6, // 13 lt magenta
    0xF2E4AC, // 14 yellow
    0xFFFFFF, // 15 white
};

const uint32_t bgDark[16] =        // classic bg -> dark-theme bg
{
    0x0D1117, // 0  black         -> near-black
    0x152238, // 1  blue          -> dark navy (desktop, blue windows)
    0x132D1D, // 2  green         -> dark green
    0x113333, // 3  cyan          -> dark teal (frames)
    0x391A16, // 4  red           -> dark red
    0x2E1A33, // 5  magenta       -> dark purple
    0x3A2E16, // 6  brown         -> dark amber
    0x252B33, // 7  light gray    -> dark slate (menus, dialogs, windows)
    0x3A444E, // 8  dark gray     -> raised slate
    0x2A3A52, // 9  lt blue
    0x2C4636, // 10 lt green
    0x2A4A4A, // 11 lt cyan
    0x4A3230, // 12 lt red
    0x45304A, // 13 lt magenta
    0x4E4224, // 14 yellow
    0x343B44, // 15 white      -> light slate
};

// Re-color one classic palette byte (BIOS fg in the low nibble, BIOS bg
// in the high nibble) for the dark theme.
TColorAttr darkAttr( uchar biosByte )
{
    uchar fg = biosByte & 0x0F;
    uchar bg = (biosByte >> 4) & 0x0F;
    return TColorAttr( TColor( int(fgDark[fg]) ),
                       TColor( int(bgDark[bg]) ) );
}

// The classic Turbo Vision application palette (cpAppColor), rebuilt as
// the default TProgram::getPalette() does.
TPalette& classicPalette()
{
    static TPalette palette( cpAppColor, sizeof( cpAppColor ) - 1 );
    return palette;
}

// The dark application palette: the classic cpAppColor bytes with every
// entry re-colored through darkAttr(). Same layout, same length (135).
TPalette& darkPalette()
{
    static TPalette palette = []() -> TPalette
    {
        static const char *src = cpAppColor;
        TColorAttr entries[ sizeof( cpAppColor ) - 1 ];
        for( size_t i = 0; i < sizeof( cpAppColor ) - 1; ++i )
            entries[i] = darkAttr( uchar(src[i]) );
        return TPalette( entries, sizeof( entries ) / sizeof( entries[0] ) );
    }();
    return palette;
}

} // anonymous namespace

Theme currentTheme()
{
    initTheme_();
    return g_theme_;
}

const char *themeName( Theme theme )
{
    return theme == ThemeDark ? "dark" : "classic";
}

bool setTheme( Theme theme )
{
    initTheme_();
    if( theme == g_theme_ ) return false;
    g_theme_ = theme;
    return true;
}

TPalette& themePalette()
{
    initTheme_();
    return g_theme_ == ThemeDark ? darkPalette() : classicPalette();
}

} // namespace atui::tui
} // namespace atui