// Color theme implementation. See theme.h.

#define Uses_TPalette
#define Uses_TProgram
#include <tvision/tv.h>

#include <cstddef>
#include <cstdlib>

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
    // std::, not bare: Watcom's <cstdlib> keeps the C names in std where
    // they belong, so `getenv` alone does not compile there (measured).
    if( isClassicEnv( std::getenv( "ATUI_THEME" ) ) )
        g_theme_ = ThemeClassic;
}

/* ------------------------------------------------------------------ colors */

/* The sixteen BIOS colors by name. An attribute byte is a background nibble
   and a foreground nibble, and 0x8E reads as "yellow on dark gray" only to
   somebody who has memorized the numbering. */
enum BiosColor
{
    biosBlack, biosBlue, biosGreen, biosCyan, biosRed, biosMagenta,
    biosBrown, biosLightGray, biosDarkGray, biosLightBlue, biosLightGreen,
    biosLightCyan, biosLightRed, biosLightMagenta, biosYellow, biosWhite
};

inline uchar biosAttr( uchar fg, uchar bg )
{
    return uchar( fg | (bg << 4) );
}

/* Transcript and markdown roles as plain BIOS attribute bytes, one entry per
   role and in the order of the Role enum (see md_style.h). Both color models
   can use this table: a TColorAttr *is* an attribute byte in the legacy one,
   and the modern one can be built from a byte. This is the stock Turbo Vision
   look, so the classic theme is these colors on every platform. */
const uchar kRoleClassic[rRoleCount] =
{
    /* rNormal */      biosAttr( biosYellow,       biosBlue ),
    /* rUser */        biosAttr( biosGreen,        biosBlue ),
    /* rThinking */    biosAttr( biosLightGray,    biosBlue ),
    /* rHead */        biosAttr( biosCyan,         biosBlue ),
    /* rStatus */      biosAttr( biosLightMagenta, biosBlue ),
    /* rTool */        biosAttr( biosWhite,        biosBlue ),
    /* rToolOut */     biosAttr( biosLightGray,    biosBlue ),
    /* rHeading */     biosAttr( biosYellow,       biosBlue ),
    /* rBold */        biosAttr( biosWhite,        biosBlue ),
    /* rItalic */      biosAttr( biosLightGray,    biosBlue ),
    /* rBoldItalic */  biosAttr( biosLightGray,    biosBlue ),
    /* rCodeInline */  biosAttr( biosBlue,         biosLightGray ),
    /* rLink */        biosAttr( biosLightBlue,    biosBlue ),
    /* rQuote */       biosAttr( biosLightGray,    biosBlue ),
    /* rMarker */      biosAttr( biosLightCyan,    biosBlue ),
    /* rStrike */      biosAttr( biosLightGray,    biosBlue ),
    /* rDim */         biosAttr( biosLightGray,    biosBlue ),
    /* rCode */        biosAttr( biosLightGray,    biosBlue ),
    /* rKeyword */     biosAttr( biosYellow,       biosBlue ),
    /* rType */        biosAttr( biosGreen,        biosBlue ),
    /* rString */      biosAttr( biosLightMagenta, biosBlue ),
    /* rChar */        biosAttr( biosLightMagenta, biosBlue ),
    /* rComment */     biosAttr( biosDarkGray,     biosBlue ),
    /* rNumber */      biosAttr( biosWhite,        biosBlue ),
    /* rFunction */    biosAttr( biosLightCyan,    biosBlue ),
    /* rPreproc */     biosAttr( biosLightMagenta, biosBlue ),
    /* rOperator */    biosAttr( biosLightGray,    biosBlue ),
    /* rInput */       biosAttr( biosBlack,        biosLightGray ),
    /* rInputSep */    biosAttr( biosBlack,        biosLightGray )
};

/* The two color models part company here. Below this line, everything that
   mentions TColor, TColorRGB or a sl* style bit belongs to the modern model
   only, because none of those names exist in the legacy one. */

#if defined( TVISION_LEGACY_COLORS )

/* The dark theme in sixteen colors. Backgrounds collapse onto black -- the
   light gray one becomes dark gray so that dialogs, menus and window bodies
   still stand off the desktop -- and foregrounds become the bright value of
   their own hue so that they read on either.

   Two things are lost and both are inherent to sixteen colors rather than to
   this table: entries that were distinguished only by a saturated background
   (green, cyan) now share the black one, and a dark gray foreground, which is
   how Turbo Vision marks text that is there but not active, has to become
   light gray because dark gray is in use as the window background. What the
   interface depends on is kept: light text on dark backgrounds, and enough of
   a difference between desktop, window and selected item to tell them apart.
   test_theme_bios_dark() checks that no palette entry ends up reading one
   color on the same color. */

const uchar kDarkFg[16] =       // classic foreground -> dark theme foreground
{
    biosLightGray,              //  0 black       -> light text
    biosLightBlue,              //  1 blue
    biosLightGreen,             //  2 green
    biosLightCyan,              //  3 cyan
    biosLightRed,               //  4 red
    biosLightMagenta,           //  5 magenta
    biosYellow,                 //  6 brown       -> yellow, the loud brown
    biosLightGray,              //  7 light gray
    biosLightGray,              //  8 dark gray   -> see the note above
    biosLightBlue,              //  9  light blue
    biosLightGreen,             // 10 light green
    biosLightCyan,              // 11 light cyan
    biosLightRed,               // 12 light red
    biosLightMagenta,           // 13 light magenta
    biosYellow,                 // 14 yellow
    biosWhite                   // 15 white
};

const uchar kDarkBg[16] =       // classic background -> dark theme background
{
    biosBlack,                  //  0 black
    biosBlack,                  //  1 blue        -> the desktop
    biosBlack,                  //  2 green
    biosBlack,                  //  3 cyan
    biosBlack,                  //  4 red
    biosBlack,                  //  5 magenta
    biosBlack,                  //  6 brown
    biosDarkGray,               //  7 light gray  -> windows, dialogs, menus
    biosDarkGray,               //  8 dark gray   -> anything brighter stays a
    biosDarkGray,               //  9 light blue     surface rather than turning
    biosDarkGray,               // 10 light green    into a light background
    biosDarkGray,               // 11 light cyan
    biosDarkGray,               // 12 light red
    biosDarkGray,               // 13 light magenta
    biosDarkGray,               // 14 yellow
    biosDarkGray                // 15 white
};

// Re-color one classic palette byte (BIOS fg in the low nibble, BIOS bg in the
// high nibble) for the dark theme.
inline uchar biosDarkByte( uchar biosByte )
{
    return biosAttr( kDarkFg[ biosByte & 0x0F ],
                     kDarkBg[ (biosByte >> 4) & 0x0F ] );
}

/* The dark roles. Unlike the palette, these are pairs chosen together rather
   than two channels mapped apart, so the losses above do not apply: de-emphasized
   text is dark gray on the black prose background, and a code block keeps a
   background of its own. Bold and italic have no bits in an attribute byte; the
   brighter color is the emphasis, and yellow is already the bright yellow.

   The code background is dark gray, which is attribute bit 7 -- on OS/2's own
   display and on any ANSI terminal that is a slightly lighter black, exactly
   what a code panel wants. A text-mode VGA set to blink rather than intensity
   will blink a code block instead; nothing else in either theme sets that bit. */
const uchar kRoleDark[rRoleCount] =
{
    /* rNormal */      biosAttr( biosLightGray,    biosBlack    ),
    /* rUser */        biosAttr( biosLightGreen,   biosBlack    ),
    /* rThinking */    biosAttr( biosDarkGray,     biosBlack    ),
    /* rHead */        biosAttr( biosLightCyan,    biosBlack    ),
    /* rStatus */      biosAttr( biosYellow,       biosBlack    ),
    /* rTool */        biosAttr( biosWhite,        biosBlack    ),
    /* rToolOut */     biosAttr( biosLightGray,    biosBlack    ),
    /* rHeading */     biosAttr( biosYellow,       biosBlack    ),
    /* rBold */        biosAttr( biosWhite,        biosBlack    ),
    /* rItalic */      biosAttr( biosLightCyan,    biosBlack    ),
    /* rBoldItalic */  biosAttr( biosWhite,        biosBlack    ),
    /* rCodeInline */  biosAttr( biosBrown,        biosDarkGray ),
    /* rLink */        biosAttr( biosLightBlue,    biosBlack    ),
    /* rQuote */       biosAttr( biosDarkGray,     biosBlack    ),
    /* rMarker */      biosAttr( biosLightCyan,    biosBlack    ),
    /* rStrike */      biosAttr( biosDarkGray,     biosBlack    ),
    /* rDim */         biosAttr( biosDarkGray,     biosBlack    ),
    /* rCode */        biosAttr( biosLightGray,    biosDarkGray ),
    /* rKeyword */     biosAttr( biosLightRed,     biosDarkGray ),
    /* rType */        biosAttr( biosLightGreen,   biosDarkGray ),
    /* rString */      biosAttr( biosYellow,       biosDarkGray ),
    /* rChar */        biosAttr( biosYellow,       biosDarkGray ),
    /* rComment */     biosAttr( biosLightBlue,    biosDarkGray ),
    /* rNumber */      biosAttr( biosLightCyan,    biosDarkGray ),
    /* rFunction */    biosAttr( biosWhite,        biosDarkGray ),
    /* rPreproc */     biosAttr( biosLightMagenta, biosDarkGray ),
    /* rOperator */    biosAttr( biosLightGray,    biosDarkGray ),
    /* rInput */       biosAttr( biosLightGray,    biosBlack    ),
    /* rInputSep */    biosAttr( biosDarkGray,     biosBlack    )
};

TColorAttr darkRoleAttr( int role )
{
    return TColorAttr( kRoleDark[role] );
}

#else // the modern color model: RGB colors and style bits

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

// The dark roles as RGB. Transcript text sits on one of two backgrounds: the
// prose background, and a slightly darker one for code.
const uint32_t kPaneBg = 0x152238;   // dark navy background
const uint32_t kCodeBg = 0x0C1626;   // code background (slightly darker)

struct RgbRole
{
    uint32_t fg;
    uint32_t bg;
    ushort style;
};

const RgbRole kRoleDark[rRoleCount] =
{
    /* rNormal */      { 0xF2E4AC, kPaneBg, 0                    },
    /* rUser */        { 0x9BD49E, kPaneBg, 0                    },
    /* rThinking */    { 0x8A94A0, kPaneBg, 0                    },
    /* rHead */        { 0x74CFCF, kPaneBg, 0                    },
    /* rStatus */      { 0xE9CB94, kPaneBg, 0                    },
    /* rTool */        { 0x9CDCF0, kPaneBg, slBold                },
    /* rToolOut */     { 0xB6C2CF, kPaneBg, 0                    },
    /* rHeading */     { 0xF7D488, kPaneBg, slBold                },
    /* rBold */        { 0xF5EDCB, kPaneBg, slBold                },
    /* rItalic */      { 0xCFE3B8, kPaneBg, slItalic              },
    /* rBoldItalic */  { 0xF5EDCB, kPaneBg, slBold | slItalic     },
    /* rCodeInline */  { 0xE8BFA0, kCodeBg, 0                    },
    /* rLink */        { 0x7FB6FF, kPaneBg, slUnderline           },
    /* rQuote */       { 0xA9B2BC, kPaneBg, 0                    },
    /* rMarker */      { 0x74CFCF, kPaneBg, 0                    },
    /* rStrike */      { 0x7E8794, kPaneBg, 0                    },
    /* rDim */         { 0x8A94A0, kPaneBg, 0                    },
    /* rCode */        { 0xE6E1CE, kCodeBg, 0                    },
    /* rKeyword */     { 0xF08B9A, kCodeBg, 0                    },
    /* rType */        { 0x7FD1A6, kCodeBg, 0                    },
    /* rString */      { 0xC9C17A, kCodeBg, 0                    },
    /* rChar */        { 0xC9C17A, kCodeBg, 0                    },
    /* rComment */     { 0x6E7683, kCodeBg, slItalic              },
    /* rNumber */      { 0xE0A86B, kCodeBg, 0                    },
    /* rFunction */    { 0x87B0F0, kCodeBg, 0                    },
    /* rPreproc */     { 0xB88AD0, kCodeBg, 0                    },
    /* rOperator */    { 0xC8C4B6, kCodeBg, 0                    },
    /* rInput */       { 0xF2E4AC, kPaneBg, 0                    },
    /* rInputSep */    { 0x3A4A63, kPaneBg, 0                    }
};

TColorAttr darkRoleAttr( int role )
{
    const RgbRole &r = kRoleDark[role];
    return TColorAttr( TColor( int(r.fg) ), TColor( int(r.bg) ), r.style );
}

#endif // color model

// The classic Turbo Vision application palette (cpAppColor), rebuilt as
// the default TProgram::getPalette() does.
TPalette& classicPalette()
{
    static TPalette palette( cpAppColor, sizeof( cpAppColor ) - 1 );
    return palette;
}

#if defined( TVISION_LEGACY_COLORS )

// The dark application palette in sixteen colors: the classic cpAppColor bytes
// with every entry re-colored through biosDarkByte(). Same layout, same length
// (135). TPalette copies the bytes it is given, and in this model a byte is an
// attribute, so the entries are a char array.
TPalette& darkPalette()
{
    static char entries[ sizeof( cpAppColor ) ];   // one spare for the NUL
    static bool built = false;
    if( !built )
    {
        for( size_t i = 0; i < sizeof( cpAppColor ) - 1; ++i )
            entries[i] = char( biosDarkByte( uchar( cpAppColor[i] ) ) );
        built = true;
    }
    static TPalette palette( entries, ushort( sizeof( cpAppColor ) - 1 ) );
    return palette;
}

#else

// The dark application palette: the classic cpAppColor bytes with every
// entry re-colored through darkAttr(). Same layout, same length (135).
struct DarkEntries
{
    TColorAttr values[ sizeof( cpAppColor ) - 1 ];
    DarkEntries()
    {
        const char *src = cpAppColor;
        for( size_t i = 0; i < sizeof( cpAppColor ) - 1; ++i )
            values[i] = darkAttr( uchar( src[i] ) );
    }
};

TPalette& darkPalette()
{
    static const DarkEntries entries;
    static TPalette palette( entries.values, ushort( sizeof( cpAppColor ) - 1 ) );
    return palette;
}

#endif // color model

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

TColorAttr roleAttr( Role role )
{
    initTheme_();
    // mapColor() hands over whatever byte a caller put in the text; anything
    // outside the table reads as normal text rather than past the end of it.
    const int i = int( role );
    if( i < 0 || i >= rRoleCount ) return TColorAttr( kRoleClassic[rNormal] );
    return g_theme_ == ThemeDark ? darkRoleAttr( i )
                                 : TColorAttr( kRoleClassic[i] );
}

} // namespace atui::tui
} // namespace atui
