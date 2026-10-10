// A callable tool exposed to the model via the OpenAI "tools" interface.
//
// A tool is: a name the model can call, a natural-language description and a
// JSON-Schema describing its arguments (both sent to the model so it knows how
// to invoke it), and a synchronous execute() that turns the JSON arguments the
// model produced into a plain-text result string returned to the model.
//
// C++14. No networking dependency: tools are pure local functions.
#ifndef ATUI_TOOLS_TOOL_H
#define ATUI_TOOLS_TOOL_H

#include <string>

namespace atui {
namespace tools {

class Tool {
public:
    virtual ~Tool() {}

    // Identifier the model calls; must match [a-zA-Z0-9_-].
    virtual const char *name() const = 0;

    // Tells the model what the tool does and when to use it.
    virtual const char *description() const = 0;

    // JSON Schema (an object) for the arguments, emitted verbatim into the
    // request's "tools" array. e.g. {"type":"object","properties":{},"required":[]}
    virtual const char *parametersSchema() const = 0;

    // Run the tool with the raw JSON arguments string the model emitted and
    // return a text result. Must not throw for robustness.
    virtual std::string execute( const std::string &argumentsJson ) = 0;

    // Whether running this tool must be approved by the user first. Tools that
    // touch the file system or run commands return true; the chat loop then
    // consults the process-wide Approver before executing. Defaults to false.
    virtual bool requiresApproval() const { return false; }

    // A short, human-readable one-line preview of what this specific call will
    // do, shown in the confirmation prompt. `argumentsJson` is the raw
    // arguments. Defaults to just the tool name.
    virtual std::string approvalSummary( const std::string &argumentsJson ) const
    {
        ( void ) argumentsJson;
        return std::string( name() );
    }

    // How this call should read in the transcript, where it stays as the record
    // of what was actually done (unlike approvalSummary, which is prose for a
    // dialog that disappears). May span several lines: the transcript indents
    // them. Defaults to the tool name.
    virtual std::string callPreview( const std::string &argumentsJson ) const
    {
        ( void ) argumentsJson;
        return std::string( name() );
    }
};

} // namespace atui::tools
} // namespace atui

#endif // ATUI_TOOLS_TOOL_H
