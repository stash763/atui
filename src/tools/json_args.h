// A tiny reader for the flat JSON object the model emits as a tool call's
// `arguments` string, e.g. {"path":"a.txt","old_text":"x","all":true}.
//
// Tool arguments are always a single JSON object whose members we read by key.
// This is intentionally minimal but careful: it understands string escapes so
// a value containing braces, quotes or newlines never confuses the scan, and
// it skips nested objects/arrays without interpreting them.
//
// C++14. Network-independent.
#ifndef ATUI_TOOLS_JSON_ARGS_H
#define ATUI_TOOLS_JSON_ARGS_H

#include <string>

namespace atui {
namespace tools {

// Reads a top-level string member `key` from JSON object `obj`, unescaping it
// (so "\n" becomes a newline, "\"" a quote, and so on). Returns true when the
// key is present and its value is a string.
bool jsonStringArg( const std::string &obj, const std::string &key,
                    std::string &out );

// Reads a top-level boolean member `key`. Accepts true/false (any case) and
// also the strings "true"/"false"/"1"/"0". Returns true when found and
// parseable.
bool jsonBoolArg( const std::string &obj, const std::string &key, bool &out );

// Returns true when `obj` parses as a JSON object (first non-space char '{'
// and a matching '}'). Used to reject garbage arguments early.
bool looksJsonObject( const std::string &obj );

} // namespace atui::tools
} // namespace atui

#endif // ATUI_TOOLS_JSON_ARGS_H
