// edit_file: modify an existing text file by replacing an exact substring.
//
// Replaces the first occurrence of `old_text` with `new_text`, or every
// occurrence when `all` is true. The file must already exist (creating files
// is not this tool's job). Requires user approval before it runs.
//
// C++14.
#ifndef ATUI_TOOLS_EDIT_FILE_TOOL_H
#define ATUI_TOOLS_EDIT_FILE_TOOL_H

#include "tools/tool.h"

namespace atui {
namespace tools {

class EditFileTool : public Tool {
public:
    const char *name() const override;
    const char *description() const override;
    const char *parametersSchema() const override;
    std::string execute( const std::string &argumentsJson ) override;
    bool requiresApproval() const override { return true; }
    std::string approvalSummary( const std::string &argumentsJson ) const override;
};

// Shared instance used by registerDefaults().
extern EditFileTool g_editFileTool;

} // namespace atui::tools
} // namespace atui

#endif // ATUI_TOOLS_EDIT_FILE_TOOL_H
