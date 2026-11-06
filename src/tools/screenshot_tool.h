// screenshot: grab the screen (or one window) into a BMP file the user can
// open and the model can point at.
//
// The result is a path, not pixels: this build's transport carries text,
// and a base64 picture in a transcript that shows seven-bit ASCII would be
// the worst possible way to be there. The window listing tool is the
// model's text-shaped eyes; this tool is the evidence it files.
//
// C++98.
#ifndef ATUI_TOOLS_SCREENSHOT_TOOL_H
#define ATUI_TOOLS_SCREENSHOT_TOOL_H

#include "tools/tool.h"

namespace atui {
namespace tools {

class ScreenshotTool : public Tool {
public:
    const char *name() const;
    const char *description() const;
    const char *parametersSchema() const;
    std::string execute( const std::string &argumentsJson );

    // A screenshot is the whole screen: whatever the windows tool can
    // name, the capture also takes everything else that is showing. That
    // is worth a person's yes each time, so the permission subject says
    // "cannot be reasoned about", which the policy reads as "ask".
    bool requiresApproval() const;
    PermissionSubject permissionSubject( const std::string &argumentsJson ) const;
    std::string approvalSummary( const std::string &argumentsJson ) const;
    std::string callPreview( const std::string &argumentsJson ) const;
};

// Shared instance used by registerDefaults().
extern ScreenshotTool g_screenshotTool;

} // namespace atui::tools
} // namespace atui

#endif // ATUI_TOOLS_SCREENSHOT_TOOL_H
