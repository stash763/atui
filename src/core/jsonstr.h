// The JSON string decoder.  One implementation for the whole program.
//
// There were three -- one in the tool-argument reader, one in the network
// reader, one inline in `deltaContent` -- and that is how one of them came to be
// missing `\u` altogether: a model answer containing `caf\u00e9` arrived as the
// eleven characters `cafu00e9`, on every platform, from any server that escapes
// non-ASCII, which several do.  Two of the three also disagreed about `\/`.
// Both now decode here, so they cannot drift again.
//
// Two rules, both deliberate:
//
//   Unknown escapes are kept whole.  `\U` decodes to the two characters `\` and
//   `U`.  The alternative -- keep the letter, throw the backslash away, which is
//   what this used to do -- turns `C:\Users\stash` into `C:Usersstash`, so a
//   model that answers with a Windows path, which is what Windows users write,
//   gets a tool call against a file that does not exist.  A tolerant parser may
//   accept input the grammar forbids; it may not quietly mean something else.
//   Conformant input is untouched, because a backslash before a character that
//   cannot escape has no defined meaning to preserve *against*.
//
//   Decoding continues past a problem.  A `\u` with three hex digits is
//   reported as malformed *and* decoded as far as it can be, so a caller reading
//   a stream it does not control can keep the rest of the message instead of
//   losing it.  Whether a partial result is worth anything is the caller's
//   judgement, not a reason to withhold it.
//
// C++98 and exception-free, like the rest of core/.

#ifndef ATUI_CORE_JSONSTR_H
#define ATUI_CORE_JSONSTR_H

#include <cstddef>
#include <string>

namespace atui {
namespace core {

// Decodes the JSON string literal that begins at s[i], which must be the opening
// '"'.  Appends the text the literal denotes to `out` and leaves i at the first
// character after the literal -- at `end` when there is no closing quote.
//
// Returns false when the literal is not well formed: unterminated, a trailing
// backslash, a `\u` without four hex digits, or a surrogate that is not paired.
// An unknown escape is NOT reported, and that distinction is the whole design:
// `\U` was accepted deliberately, so a tool call whose argument is
// `C:\Users\stash` decodes and works instead of failing on a technicality.
// Note that it returns false having appended what it could decode, so callers
// that would rather have damaged text than none can ignore the answer; callers
// that refuse the whole value must check it.  `end` exists because the network
// reader decodes inside a range of a larger buffer, and a literal must not be
// read past the object it came in.
bool decodeJsonString( const std::string &s, std::size_t &i,
                       std::size_t end, std::string &out );

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_JSONSTR_H
