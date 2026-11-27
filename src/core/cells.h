// How much of the screen a piece of text takes, in columns.
//
// atui's internal form is UTF-8, and UTF-8 makes a string's byte length a function of
// which characters it holds rather than of how much text it is: 'é' is two bytes, an em
// dash three, an ideograph three.  So a byte count cannot be used to pad, wrap, align or
// place a cursor.  On an 8-bit screen the two were the same number and the mistake was
// invisible; on a Unicode console a three-byte em dash laid out in three columns draws
// one glyph and leaves two blanks, and an ideograph charged three columns against the two
// the terminal draws it in drifts everything after it.
//
// This is not a Windows defect.  The wrapping rule used to say it out loud -- "One byte
// is one display column (the app never measured cells)" -- and every platform that folds
// to ASCII before drawing (OS/2, DOS) still hides it, because folded text is one byte per
// column by construction.  Folding is not the fix: it makes the two numbers agree by
// destroying the text that is being laid out.
//
// The answer here is a table rather than a question to the live console, for two reasons.
// A layout has to be reproducible -- it is checked by a self-test that runs with no
// terminal at all -- and every caller in one frame has to get the same answer, or the
// transcript disagrees with itself about where a row ends.  The table matches what was
// measured on a Windows 10 console (`x日本y` advances 6 cells, `x—y` advances 3,
// `xcéf y` advances 5), and where a table and a terminal could disagree it charges more,
// so the failure mode is a row that wraps a little early rather than text drawn past the
// column it owns.
//
// What the table does not claim: Indic and a few other combining blocks are absent, so a
// combining mark there counts one column instead of none, and a Devanagari transcript
// wraps slightly early.  That is an over-count, which is the safe direction, and adding
// the block is a table edit rather than a design change.
#ifndef ATUI_CORE_CELLS_H
#define ATUI_CORE_CELLS_H

#include <cstddef>
#include <string>

namespace atui {
namespace core {

// Columns used by the character whose UTF-8 begins at `i`: 0 for a combining mark or
// zero-width joiner, 2 for an ideograph or fullwidth form, 1 for anything else.
// `bytes` receives the length of the sequence -- always at least 1, so a caller can step
// to the next character without decoding twice.
//
// Bytes that are not a valid UTF-8 sequence count as one byte and one column, which is
// what the screen will make of them: a decoder that invented 0 would let a row overrun
// the width it was given, and one that rejected the string would refuse to lay out text
// it still has to display.
int charCells( const std::string &s, std::size_t i, std::size_t *bytes );

// Total columns for a whole UTF-8 string: what a row of this text occupies.
int displayWidth( const std::string &s );

// The longest prefix of `s` that fits in `columns`, ending at a character boundary.
// Clipping at a byte count cuts a character in half; clipping at this measure is the
// same number the row was wrapped and padded with, which is the only version of "fit"
// that agrees with what is drawn.
std::string clipToColumns( const std::string &s, int columns );

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_CELLS_H
