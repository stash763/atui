// Implementation of the column measure. See cells.h for why the numbers are what they are.
#include "core/cells.h"

#include <cstddef>

namespace atui {
namespace core {

namespace {

struct Run
{
    unsigned long first;
    unsigned long last;
};

// Two tables rather than one with a width per row, because a binary search needs them
// sorted by code point and a human reading them wants them grouped by answer.  Anything
// in neither table is one column, and that is most of what a conversation is written in:
// ASCII, the ISO Latin supplements, Greek, Cyrillic, Hebrew, Arabic, Thai, and the
// punctuation and symbol blocks -- including the East Asian "ambiguous" class, which the
// target console measured as one column (em dash, arrow, curly quote, ellipsis).
//
// Both tables are ascending and disjoint.  Where a range could not be settled from the
// measurement it was left out, which charges one column: for a mark that is an over-count
// of one, and the safe direction.  Indic combining marks are the honest gap -- a
// Devanagari transcript wraps slightly early -- and adding the block is a table edit,
// which the project's English-only display scope (WINDOWS_PORT.md) keeps as an edit nobody
// is queued to make rather than a defect waiting to be fixed.

// Charged nothing: they belong to the character before them.
const Run kZero[] = {
    { 0x0300, 0x036F },     // combining diacritical marks
    { 0x0483, 0x0489 },     // Cyrillic marks
    { 0x0591, 0x05BD },     // Hebrew points
    { 0x05BF, 0x05BF },
    { 0x05C1, 0x05C2 },
    { 0x05C4, 0x05C5 },
    { 0x05C7, 0x05C7 },
    { 0x0610, 0x061A },     // Arabic marks
    { 0x064B, 0x065F },
    { 0x0670, 0x0670 },
    { 0x06D6, 0x06DC },
    { 0x06DF, 0x06E4 },
    { 0x06E7, 0x06E8 },
    { 0x06EA, 0x06ED },
    { 0x0711, 0x0711 },     // Syriac, Thaana, Middle Persian marks
    { 0x0730, 0x074A },
    { 0x07A6, 0x07B0 },
    { 0x0816, 0x0819 },
    { 0x081B, 0x0823 },
    { 0x0825, 0x0827 },
    { 0x0829, 0x082D },
    { 0x1AB0, 0x1AFF },     // combining diacritical marks, extended
    { 0x1DC0, 0x1DFF },     // combining diacritical marks, supplement
    { 0x200B, 0x200F },     // zero width space, the bidi marks, the two joiners
    { 0x202A, 0x202E },     // bidi embedding and override controls
    { 0x2060, 0x2064 },     // word joiner and the invisible operators
    { 0x20D0, 0x20FF },     // marks over symbols (a keycap's U+20E3 is one of these)
    { 0xFE00, 0xFE0E },     // variation selectors other than emoji presentation
    { 0xFE20, 0xFE2F },     // combining half marks
    { 0xFEFF, 0xFEFF }      // zero width no-break space
};

// Charged two columns: ideographs, kana, fullwidth forms, emoji.
const Run kWide[] = {
    { 0x1100, 0x115F },     // Hangul jamo, initial consonants
    { 0x2E80, 0x303E },     // CJK and Kangxi radicals, CJK symbols (U+303F is narrow)
    { 0x3041, 0x33FF },     // kana, bopomofo, Hangul compatibility jamo, CJK compatibility
    { 0x3400, 0x4DBF },     // CJK unified ideographs, extension A
    { 0x4E00, 0x9FFF },     // CJK unified ideographs
    { 0xA000, 0xA4CF },     // Yi syllables and radicals
    { 0xA960, 0xA97C },     // Hangul jamo extended A
    { 0xAC00, 0xD7A3 },     // Hangul syllables
    { 0xF900, 0xFAFF },     // CJK compatibility ideographs
    { 0xFE10, 0xFE19 },     // vertical forms
    { 0xFE30, 0xFE6F },     // CJK compatibility forms
    { 0xFF00, 0xFF60 },     // fullwidth forms
    { 0xFFE0, 0xFFE6 },     // fullwidth signs

    { 0x17000, 0x18AFF },   // Tangut and its supplies
    { 0x1B000, 0x1B152 },   // kana supplement, small kana extension
    { 0x1B164, 0x1B167 },
    { 0x1B170, 0x1B2FB },   // Nushu

    { 0x1F004, 0x1F004 },   // red dragon and the three other pictographs that sit below
    { 0x1F0CF, 0x1F0CF },   // the pictograph planes
    { 0x1F18E, 0x1F18E },
    { 0x1F191, 0x1F19A },
    { 0x1F300, 0x1F64F },   // symbols and pictographs I, emoticons, pictographs II
    { 0x1F680, 0x1F6FF },   // transport and map symbols
    { 0x1F900, 0x1F9FF },   // symbols and pictographs, supplemental
    { 0x1FA70, 0x1FAFF },   // symbols and pictographs, extended A

    { 0x20000, 0x2FFFD },   // CJK unified ideographs, extensions B through F
    { 0x30000, 0x3FFFD }    // and whatever is ratified above them
};

const std::size_t kZeroCount = sizeof( kZero ) / sizeof( kZero[0] );
const std::size_t kWideCount = sizeof( kWide ) / sizeof( kWide[0] );

// A width is asked once per character of a transcript that can run to tens of thousands
// of characters, so this is the coldest loop in the layout and it searches rather than
// walks.  `found` says whether the code point fell in the table; the caller's answer for
// anything outside both tables is one column.
bool inRuns( const Run *runs, std::size_t n, unsigned long cp )
{
    std::size_t lo = 0, hi = n;
    while( lo < hi )
    {
        const std::size_t mid = lo + (hi - lo) / 2;
        if( cp < runs[mid].first )
            hi = mid;
        else if( cp > runs[mid].last )
            lo = mid + 1;
        else
            return true;
    }
    return false;
}

} // anonymous namespace

int charCells( const std::string &s, std::size_t i, std::size_t *bytes )
{
    if( bytes )
        *bytes = 1;
    if( i >= s.size() )
        return 0;

    const unsigned char lead = (unsigned char) s[i];
    if( lead < 0x80 )
        return 1;                       // ASCII, control characters included

    std::size_t want = 0;
    if( lead >= 0xC2 && lead <= 0xDF )      want = 2;
    else if( lead >= 0xE0 && lead <= 0xEF ) want = 3;
    else if( lead >= 0xF0 && lead <= 0xF4 ) want = 4;
    // 0x80-0xBF is a continuation byte with no lead of its own, 0xC0-0xC1 could only
    // begin an overlong form, and 0xF5-0xFF cannot begin a sequence at all.  None of them
    // is worth arguing with: they cost the one column the screen will spend on them, and
    // claiming 0 would let a row overrun the width it was given.
    if( want == 0 )
        return 1;
    if( i + want > s.size() )
        return 1;                       // sequence cut short by the end of the string

    unsigned long cp = (unsigned long) (lead & (0xFF >> (want + 1)));
    for( std::size_t k = 1; k < want; ++k )
    {
        const unsigned char follow = (unsigned char) s[i + k];
        if( (follow & 0xC0) != 0x80 )
            return 1;                   // not a continuation byte: the lead stands alone
        cp = (cp << 6) | (unsigned long) (follow & 0x3F);
    }

    if( bytes )
        *bytes = want;

    // Variation selector-16 (emoji presentation) is in neither table on purpose, so it
    // costs one column: the sequence it marks -- U+26A0 U+FE0F -- is drawn two columns
    // wide by every terminal that honours it, and a keycap's U+20E3 is a combining mark
    // that costs nothing.  Selector-15 (text presentation) is charged nothing.
    if( inRuns( kZero, kZeroCount, cp ) )
        return 0;
    if( inRuns( kWide, kWideCount, cp ) )
        return 2;
    return 1;
}

int displayWidth( const std::string &s )
{
    int w = 0;
    std::size_t i = 0;
    while( i < s.size() )
    {
        std::size_t step = 1;
        w += charCells( s, i, &step );
        i += step;
    }
    return w;
}

std::string clipToColumns( const std::string &s, int columns )
{
    std::string out;
    if( columns <= 0 )
        return out;
    int used = 0;
    for( std::size_t i = 0; i < s.size(); )
    {
        std::size_t bytes = 1;
        const int c = charCells( s, i, &bytes );
        // A character that would cross the edge is left out rather than split, and the
        // column it would have used is left blank: the alternative is half a glyph.
        if( used + c > columns )
            break;
        out.append( s, i, bytes );
        used += c;
        i += bytes;
    }
    return out;
}

} // namespace atui::core
} // namespace atui
