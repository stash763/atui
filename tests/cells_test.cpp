// Tests for text measured in screen columns instead of in bytes.
//
// These exist because the wrapping rule used to state the wrong thing out loud --
// "One byte is one display column (the app never measured cells)" -- which holds on an
// 8-bit screen and does not hold on a Unicode one.  The port found it on Windows 10 by
// rendering rows that line up only if widths are charged correctly: a three-byte em dash
// took three columns and drew one glyph, and an ideograph took three columns against the
// two the console drew it in.  The measurement and the console's own arithmetic are in
// `TESTING.md`; what the terminal does with the characters used here is not a guess:
// `x日本y` advances 6 cells, `x—y` advances 3, `xcéf y` advances 5.
//
// The strings are written with hex escapes for the same reason `ascii_fold_test.cpp`
// uses them: a UTF-8 source file would let the test's own bytes disagree with whatever
// the machine building it thinks an source character is.
//
// The width oracle below is deliberately written again in this file rather than borrowed
// from the code under test: a test that measures with the same table it is checking can
// only ever agree with itself.
#include "harness.h"

#include "core/cells.h"
#include "core/strutil.h"
#include "tui/markdown.h"
#include "tui/md_style.h"
#include "tui/wrap.h"

#include <string>
#include <vector>

namespace atui_test {
namespace {

// Encodes one code point as UTF-8, so a test can name a character exactly.
std::string utf8( unsigned long cp )
{
    std::string s;
    if( cp < 0x80 )
        s += (char) cp;
    else if( cp < 0x800 )
    {
        s += (char) (0xC0 | (cp >> 6));
        s += (char) (0x80 | (cp & 0x3F));
    }
    else if( cp < 0x10000 )
    {
        s += (char) (0xE0 | (cp >> 12));
        s += (char) (0x80 | ((cp >> 6) & 0x3F));
        s += (char) (0x80 | (cp & 0x3F));
    }
    else
    {
        s += (char) (0xF0 | (cp >> 18));
        s += (char) (0x80 | ((cp >> 12) & 0x3F));
        s += (char) (0x80 | ((cp >> 6) & 0x3F));
        s += (char) (0x80 | (cp & 0x3F));
    }
    return s;
}

// True when `s` is a whole sequence of UTF-8 characters: no truncated sequence at the
// end, no continuation byte without its lead, no lead byte reserved for an overlong
// form.  Wrapping that cuts a row at a byte boundary fails this, which is the whole
// point -- half a character on screen is mojibake, and half a character sent to a model
// is corrupt text.
bool wholeChars( const std::string &s )
{
    size_t i = 0;
    while( i < s.size() )
    {
        unsigned char c = (unsigned char) s[i];
        size_t want = 0;
        if( c < 0x80 )
            want = 1;
        else if( c >= 0xC2 && c <= 0xDF )
            want = 2;
        else if( c >= 0xE0 && c <= 0xEF )
            want = 3;
        else if( c >= 0xF0 && c <= 0xF4 )
            want = 4;
        else
            return false;                       // lone continuation or reserved lead
        if( i + want > s.size() )
            return false;                       // sequence cut in half
        for( size_t k = 1; k < want; ++k )
            if( ((unsigned char) s[i + k] & 0xC0) != 0x80 )
                return false;
        i += want;
    }
    return true;
}

// Columns a terminal gives this text -- the oracle, spelled out for the characters the
// tests use and the ranges they stand for.  Wide: ideographs, kana, fullwidth forms,
// Hangul syllables, emoji.  Zero: combining marks.  Everything else, including the
// ambiguous class an em dash belongs to, is one column, which is what was measured.
int columns( const std::string &s )
{
    int w = 0;
    size_t i = 0;
    while( i < s.size() )
    {
        unsigned char c = (unsigned char) s[i];
        unsigned long cp;
        size_t len;
        if( c < 0x80 )              { cp = c;                    len = 1; }
        else if( c < 0xE0 )         { cp = c & 0x1F;             len = 2; }
        else if( c < 0xF0 )         { cp = c & 0x0F;             len = 3; }
        else                        { cp = c & 0x07;             len = 4; }
        for( size_t k = 1; k < len && i + k < s.size(); ++k )
            cp = (cp << 6) | ((unsigned char) s[i + k] & 0x3F);
        i += len;

        if( (cp >= 0x0300 && cp <= 0x036F) )                    w += 0;
        else if( cp >= 0x200B && cp <= 0x200F )                 w += 0;
        else if( cp >= 0x20D0 && cp <= 0x20FF )                 w += 0;
        else if( cp >= 0xFE00 && cp <= 0xFE0E )                 w += 0;
        else if( cp >= 0x1100 && cp <= 0x115F )                 w += 2;
        else if( cp >= 0x2E80 && cp <= 0x303E )                 w += 2;
        else if( cp >= 0x3041 && cp <= 0x33FF )                 w += 2;
        else if( cp >= 0x3400 && cp <= 0x4DBF )                 w += 2;
        else if( cp >= 0x4E00 && cp <= 0x9FFF )                 w += 2;
        else if( cp >= 0xAC00 && cp <= 0xD7A3 )                 w += 2;
        else if( cp >= 0xF900 && cp <= 0xFAFF )                 w += 2;
        else if( cp >= 0xFF00 && cp <= 0xFF60 )                 w += 2;
        else if( cp >= 0x1F300 && cp <= 0x1F64F )               w += 2;
        else if( cp >= 0x1F680 && cp <= 0x1F6FF )               w += 2;
        else if( cp >= 0x1F900 && cp <= 0x1F9FF )               w += 2;
        else                                                    w += 1;
    }
    return w;
}

std::string repeat( const std::string &unit, int n )
{
    std::string s;
    for( int i = 0; i < n; ++i )
        s += unit;
    return s;
}

// A laid-out row as plain text: the spans with their styling forgotten.
std::string textOf( const atui::tui::Line &line )
{
    std::string s;
    for( size_t i = 0; i < line.spans.size(); ++i )
        s += line.spans[i].text;
    return s;
}

// A run of ideographs has no spaces to break on, so the wrapper hard-breaks it -- and a
// hard break taken at byte 10 of a three-byte character puts half a character on the
// screen.  This is the failure that matters most, because it is not cosmetic: the same
// rows are what the transcript would export or send back.
int test_wrapping_never_splits_a_character()
{
    const std::string ideograph = utf8( 0x65E5 );   // 日
    const std::string text = repeat( ideograph, 20 );
    std::vector<std::string> rows = atui::tui::wrapText( text, 10 );
    if( rows.empty() )
        return -1;
    for( size_t i = 0; i < rows.size(); ++i )
        if( !wholeChars( rows[i] ) )
        {
            note( "row " + atui::core::toString( (int) i )
                  + " of a wrapped ideograph run is not whole UTF-8" );
            return -2;
        }
    return 0;
}

// Every row has to fit the width it was wrapped to, counted in the columns the terminal
// will give it.  Counting bytes instead makes this fail in the other direction for
// narrow multi-byte text (a row believed to be full draws short) and for wide text (a
// row believed to fit draws past the column it owns).
int test_rows_fit_their_width_in_columns()
{
    const std::string text
        = "the " + utf8( 0x00E9 ) + " " + repeat( utf8( 0x2014 ), 3 ) + " "
        + repeat( utf8( 0x65E5 ), 6 ) + " " + repeat( "word ", 8 )
        + repeat( utf8( 0x65E5 ), 4 );
    const int width = 20;
    std::vector<std::string> rows = atui::tui::wrapText( text, width );
    if( rows.empty() )
        return -1;
    for( size_t i = 0; i < rows.size(); ++i )
    {
        if( !wholeChars( rows[i] ) )
            return -2;
        int w = columns( rows[i] );
        // One character wider than the row cannot fit and is allowed to overflow: an
        // empty row would be worse, and the old rule was the same.  A code point is at
        // most four bytes, so a longer row is more than one character and has no excuse.
        if( w > width && rows[i].size() > 4 )
        {
            note( "row " + atui::core::toString( (int) i ) + " takes "
                  + atui::core::toString( w ) + " columns in a "
                  + atui::core::toString( width ) + "-column wrap" );
            return -3;
        }
    }
    return 0;
}

// Wrapping that only counts bytes also wraps too early: a row has as many columns as the
// terminal gives its characters, not as many as it has bytes, so the transcript leaves a
// ragged right margin -- on every platform that does not fold to ASCII.
int test_ordinary_text_uses_the_width_it_was_given()
{
    // One unbreakable run of accented words: with no spaces to break on, every row but
    // the last has to fill the width exactly, which leaves a wrong measure nowhere to
    // hide.  Bytes fill 32 columns of a 40-column wrap; the user sees text that never
    // reaches the right edge.
    const std::string accent = utf8( 0x00E9 );      // é: one column, two bytes
    std::string text;
    for( int i = 0; i < 40; ++i )
        text += "caf" + accent;
    const int width = 40;
    std::vector<std::string> rows = atui::tui::wrapText( text, width );
    if( rows.size() < 2 )
        return -1;
    for( size_t i = 0; i + 1 < rows.size(); ++i )
    {
        int w = columns( rows[i] );
        if( w != width )
        {
            note( "row " + atui::core::toString( (int) i ) + " fills only "
                  + atui::core::toString( w ) + " columns of "
                  + atui::core::toString( width ) );
            return -2;
        }
    }
    return 0;
}

// The title cut is a place a character can be destroyed quietly, because the result is
// one line in a list and nobody compares it to the original.
int test_a_title_is_cut_at_a_column_not_a_byte()
{
    const std::string text = repeat( utf8( 0x65E5 ), 10 );   // 20 columns
    const std::string title = atui::core::titleFromText( text, 8 );
    if( !wholeChars( title ) )
    {
        note( "a session title cut in the middle of an ideograph" );
        return -2;
    }
    if( columns( title ) > 8 )
    {
        note( "a session title of " + atui::core::toString( columns( title ) )
              + " columns cut for 8" );
        return -3;
    }
    return 0;
}

// A laid-out row reports its own width, and the transcript uses that number to pad and
// to place the next thing, so the number has to be columns.
int test_a_laid_out_row_measures_columns()
{
    atui::tui::Line line;
    line.spans.push_back( atui::tui::Span( "caf" + utf8( 0x00E9 ), atui::tui::rNormal ) );
    line.spans.push_back( atui::tui::Span( " " + utf8( 0x2014 ) + " " + utf8( 0x65E5 ),
                                           atui::tui::rNormal ) );
    const int want = columns( "café" ) + columns( " — 日" );   // 4 + 5, because 日 is two
    if( line.width() != want )
    {
        note( "a row of " + atui::core::toString( want ) + " columns reports "
              + atui::core::toString( line.width() ) );
        return -2;
    }
    return 0;
}

// The table itself, measured against what the target console was found to do: `x日本y`
// advances 6 cells, `x—y` advances 3, `xcéf y` advances 5.  The point of testing it
// directly is that a range typed one digit wrong shifts a whole script by a column, and
// the symptom is a transcript that wraps oddly somewhere nobody thinks to look.
int test_the_width_table_follows_the_terminal()
{
    struct Case { const char *text; int want; };
    const Case cases[] = {
        { "hello",                     5 },     // ASCII: one column each
        { "a b",                       3 },
        { "caf\xC3\xA9",               4 },     // é
        { "\xE2\x80\x94",              1 },     // em dash
        { "\xE2\x86\x92",              1 },     // right arrow
        { "\xE2\x80\x9C\xE2\x80\x9D",  2 },     // curly quotes
        { "\xE2\x80\xA6",              1 },     // ellipsis
        { "\xE6\x97\xA5",              2 },     // 日
        { "\xE6\x97\xA5\xE6\x9C\xAC",  4 },     // 日本
        { "\xE3\x82\xAB\xE3\x82\xBF",  4 },     // katakana
        { "\xEA\xB5\x9C",              2 },     // Hangul syllable 한
        { "\xEF\xBC\xA1",              2 },     // fullwidth A
        { "\xF0\x9F\x9A\x80",          2 },     // rocket
        { "\xF0\x9F\x91\x8B\xF0\x9F\x8F\xBD", 4 },  // wave + skin tone modifier
        { "e\xCC\x81",                 1 },     // e + combining acute
        { "\xE2\x80\x8D",              0 },     // zero width joiner
        { "\xEF\xB8\x8E",              0 },     // variation selector-15, text style
        { "\xEF\xB8\x8F",              1 },     // variation selector-16, emoji style
        { "caf",                       3 }
    };
    for( size_t i = 0; i < sizeof( cases ) / sizeof( cases[0] ); ++i )
    {
        const std::string s( cases[i].text );
        const int got = atui::core::displayWidth( s );
        if( got != cases[i].want )
        {
            note( "case " + atui::core::toString( (int) i ) + " measured "
                  + atui::core::toString( got ) + ", expected "
                  + atui::core::toString( cases[i].want ) );
            return -2;
        }
        // The oracle in this file exists so the table cannot be checked with itself.
        if( columns( s ) != cases[i].want )
            return -3;
    }
    return 0;
}

// Bytes that are not UTF-8 still occupy the screen, so they have to occupy columns.  A
// decoder that answered 0 for a byte it could not read would let a row overrun the width
// it was given, which is worse than being one column wide in the wrong place.
int test_what_is_not_utf8_still_costs_a_column()
{
    const std::string loneContinuation( "\xA0\xA0", 2 );   // continuation bytes, no leads
    if( atui::core::displayWidth( loneContinuation ) != 2 )
        return -2;
    const std::string truncated( "\xE6\x97", 2 );     // first two bytes of 日
    if( atui::core::displayWidth( truncated ) < 1 )
        return -3;
    // And stepping has to land on the next real character rather than wander: the byte
    // count it reports is what every caller walks by.
    const std::string mixed = "a" + utf8( 0x65E5 ) + "b";
    std::size_t i = 0, steps = 0;
    while( i < mixed.size() )
    {
        std::size_t bytes = 0;
        atui::core::charCells( mixed, i, &bytes );
        if( bytes < 1 )
            return -4;
        i += bytes;
        ++steps;
        if( steps > mixed.size() )
            return -5;                        // a step of 0 would never finish
    }
    if( i != mixed.size() || steps != 3 )
        return -6;
    return 0;
}

// Breaking a row must not lose or duplicate text: the rows are the transcript, and they
// are what an export writes and a follow-up request sends back.
int test_breaking_a_row_neither_loses_nor_duplicates_text()
{
    std::string text;
    for( int i = 0; i < 30; ++i )
        text += "caf" + utf8( 0x00E9 );              // no spaces: nothing to drop
    std::vector<std::string> rows = atui::tui::wrapText( text, 23 );
    std::string joined;
    for( size_t i = 0; i < rows.size(); ++i )
        joined += rows[i];
    if( joined != text )
    {
        note( "the wrapped rows do not add up to the text that was wrapped" );
        return -2;
    }
    return 0;
}

// A markdown table places its `|` by the width of each cell, so a cell measured in bytes
// moves the border of every row that comes after it.  This asks for the obvious property:
// the separators of a rendered table all sit in the same columns.
int test_a_table_keeps_its_borders_in_the_same_columns()
{
    const std::string md
        = "| name | note |\n"
          "|:-----|:-----|\n"
          "| caf\xC3\xA9 | x |\n"
          "| \xE6\x97\xA5\xE6\x9C\xAC | y |\n"
          "| plain | z |";
    std::vector<atui::tui::Line> rows = atui::tui::render( md, 60 );

    // The property a table exists to have: the border of every row is in the same
    // column.  Measured in screen columns, because that is where a border is drawn;
    // measured against the first rendered row rather than a hand-computed number, so the
    // test does not have to know how wide the renderer chose to make a column.
    std::vector<int> first;
    int tableRows = 0;
    for( size_t r = 0; r < rows.size(); ++r )
    {
        const std::string line = textOf( rows[r] );
        std::vector<int> here;
        for( size_t i = 0; i < line.size(); )
        {
            if( line[i] == '|' )
                here.push_back( columns( line.substr( 0, i ) ) );
            std::size_t bytes = 1;
            atui::core::charCells( line, i, &bytes );
            i += bytes;
        }
        if( here.empty() )
            continue;                         // prose between tables, or a blank row
        if( first.empty() )
        {
            first = here;                     // the row every other row is measured by
            ++tableRows;
            continue;
        }
        ++tableRows;
        if( here.size() != first.size() )
        {
            note( "a table row drew " + atui::core::toString( (int) here.size() )
                  + " borders where the first row drew "
                  + atui::core::toString( (int) first.size() ) );
            return -2;
        }
        for( size_t i = 0; i < here.size(); ++i )
        {
            if( here[i] != first[i] )
            {
                note( "a table border sits at column " + atui::core::toString( here[i] )
                      + " where the first row put it at "
                      + atui::core::toString( first[i] ) );
                return -3;
            }
        }
    }
    if( tableRows < 2 )
        return -4;                            // nothing to compare: the fixture did not render
    return 0;
}

} // anonymous namespace

void init_cells_tests();
void init_cells_tests()
{
    register_test( "cells: wrapping never splits a character",
                   &test_wrapping_never_splits_a_character );
    register_test( "cells: wrapped rows fit their width in columns",
                   &test_rows_fit_their_width_in_columns );
    register_test( "cells: ordinary text uses the width it was given",
                   &test_ordinary_text_uses_the_width_it_was_given );
    register_test( "cells: a title is cut at a column not a byte",
                   &test_a_title_is_cut_at_a_column_not_a_byte );
    register_test( "cells: a laid out row measures columns",
                   &test_a_laid_out_row_measures_columns );
    register_test( "cells: the width table follows the terminal",
                   &test_the_width_table_follows_the_terminal );
    register_test( "cells: what is not utf8 still costs a column",
                   &test_what_is_not_utf8_still_costs_a_column );
    register_test( "cells: breaking a row neither loses nor duplicates text",
                   &test_breaking_a_row_neither_loses_nor_duplicates_text );
    register_test( "cells: a table keeps its borders in the same columns",
                   &test_a_table_keeps_its_borders_in_the_same_columns );
}

} // namespace atui_test
