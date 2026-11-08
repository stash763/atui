// app_close / app_click / app_set_text / app_send_msg: the window
// acts, one file each, sharing one argument shape ("handle" plus the
// act's payload). See the .cpp files and platform/appman.h for the
// measurements that bound what these may post.
//
// C++98.
#ifndef ATUI_TOOLS_APP_WINDOW_TOOLS_H
#define ATUI_TOOLS_APP_WINDOW_TOOLS_H

#include "tools/tool.h"

namespace atui {
namespace tools {

class AppCloseTool : public Tool {
public:
    const char *name() const;
    const char *description() const;
    const char *parametersSchema() const;
    std::string execute( const std::string &argumentsJson );
    bool requiresApproval() const { return true; }
    std::string approvalSummary( const std::string &argumentsJson ) const;
    std::string callPreview( const std::string &argumentsJson ) const;
};

class AppClickTool : public Tool {
public:
    const char *name() const;
    const char *description() const;
    const char *parametersSchema() const;
    std::string execute( const std::string &argumentsJson );
    bool requiresApproval() const { return true; }
    std::string approvalSummary( const std::string &argumentsJson ) const;
    std::string callPreview( const std::string &argumentsJson ) const;
};

class AppSetTextTool : public Tool {
public:
    const char *name() const;
    const char *description() const;
    const char *parametersSchema() const;
    std::string execute( const std::string &argumentsJson );
    bool requiresApproval() const { return true; }
    std::string approvalSummary( const std::string &argumentsJson ) const;
    std::string callPreview( const std::string &argumentsJson ) const;
};

class AppSendMsgTool : public Tool {
public:
    const char *name() const;
    const char *description() const;
    const char *parametersSchema() const;
    std::string execute( const std::string &argumentsJson );
    bool requiresApproval() const { return true; }
    std::string approvalSummary( const std::string &argumentsJson ) const;
    std::string callPreview( const std::string &argumentsJson ) const;
};

extern AppCloseTool g_appCloseTool;
extern AppClickTool g_appClickTool;
extern AppSetTextTool g_appSetTextTool;
extern AppSendMsgTool g_appSendMsgTool;

} // namespace atui::tools
} // namespace atui

#endif // ATUI_TOOLS_APP_WINDOW_TOOLS_H
