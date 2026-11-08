// Implementation of the four window acts. See app_window_tools.h.
//
// All four share the same posture, inherited from the measurements in
// TESTING.md: handle strings are parsed and checked before anything is
// posted, everything is posted (never sent -- a wedged target must not
// be able to wedge the chat client), and the descriptions repeat the
// landmines in writing because the machine cannot step around them
// for the model. Notably absent: any app that types characters. WM_CHAR
// into a foreign toolkit control intermittently kills the target inside
// its own code; that door stays locked until a cooperative one exists.
#include "tools/app_window_tools.h"

#include "platform/appman.h"
#include "tools/json_args.h"

namespace atui {
namespace tools {

namespace {

// Every window act starts by naming the window.
bool handleArg( const std::string &argumentsJson, std::string &out )
{
    return jsonStringArg( argumentsJson, "handle", out );
}

} // anonymous namespace

// ---------------------------------------------------------------- close

const char *AppCloseTool::name() const
{
    return "app_close";
}

const char *AppCloseTool::description() const
{
    return "Ask a window to close by posting it WM_CLOSE -- the polite "
           "door: the target gets its normal close path and can save "
           "its work or refuse. Pass the handle exactly as the windows "
           "tool printed it. Confirm with the windows tool afterwards: "
           "a refused or ignored close leaves the window listed.";
}

const char *AppCloseTool::parametersSchema() const
{
    return "{\"type\":\"object\",\"properties\":{"
           "\"handle\":{\"type\":\"string\",\"description\":"
           "\"window handle as printed by the windows tool\"}"
           "},\"required\":[\"handle\"]}";
}

std::string AppCloseTool::execute( const std::string &argumentsJson )
{
    if( !looksJsonObject( argumentsJson ) )
        return "app_close: arguments must be a JSON object";

    std::string handle;
    if( !handleArg( argumentsJson, handle ) )
        return "app_close: \"handle\" is required (list windows first)";

    std::string error;
    if( !atui::platform::appCloseWindow( handle, error ) )
        return "app_close: " + error;
    return "app_close: WM_CLOSE posted to " + handle
           + " (confirm with the windows tool)";
}

std::string AppCloseTool::approvalSummary( const std::string &argumentsJson ) const
{
    std::string handle;
    jsonStringArg( argumentsJson, "handle", handle );
    return std::string( "close window " ) + handle;
}

std::string AppCloseTool::callPreview( const std::string &argumentsJson ) const
{
    return approvalSummary( argumentsJson );
}

// ---------------------------------------------------------------- click

const char *AppClickTool::name() const
{
    return "app_click";
}

const char *AppClickTool::description() const
{
    return "Press a control's activation for it: posts WM_COMMAND with "
           "the control's own id (as the windows tool prints in child "
           "mode) to its owner, which is how standard PM buttons announce "
           "a click. Only controls that answer by command id react; "
           "windows that drive themselves by mouse messages will sit "
           "still, and nothing is injected into their input stream.";
}

const char *AppClickTool::parametersSchema() const
{
    return "{\"type\":\"object\",\"properties\":{"
           "\"handle\":{\"type\":\"string\",\"description\":"
           "\"the control's window handle\"}"
           "},\"required\":[\"handle\"]}";
}

std::string AppClickTool::execute( const std::string &argumentsJson )
{
    if( !looksJsonObject( argumentsJson ) )
        return "app_click: arguments must be a JSON object";

    std::string handle;
    if( !handleArg( argumentsJson, handle ) )
        return "app_click: \"handle\" is required (list windows with "
               "children=true first)";

    std::string error;
    if( !atui::platform::appClickWindow( handle, error ) )
        return "app_click: " + error;
    return "app_click: WM_COMMAND posted to " + handle
           + "'s owner (verify the effect with the screenshot tool)";
}

std::string AppClickTool::approvalSummary( const std::string &argumentsJson ) const
{
    std::string handle;
    jsonStringArg( argumentsJson, "handle", handle );
    return std::string( "click control " ) + handle;
}

std::string AppClickTool::callPreview( const std::string &argumentsJson ) const
{
    return approvalSummary( argumentsJson );
}

// ------------------------------------------------------------ set_text

const char *AppSetTextTool::name() const
{
    return "app_set_text";
}

const char *AppSetTextTool::description() const
{
    return "Replace a window's text through the system's marshaled "
           "WM_SETTEXT door (titles accept it; plain edit controls "
           "usually do; SPIN-family controls keep their own text and "
           "refuse). This is the only text-replacement door there is: "
           "typing characters into a foreign window by message is not "
           "offered because it kills targets.";
}

const char *AppSetTextTool::parametersSchema() const
{
    return "{\"type\":\"object\",\"properties\":{"
           "\"handle\":{\"type\":\"string\",\"description\":"
           "\"the window or control handle\"},"
           "\"text\":{\"type\":\"string\",\"description\":"
           "\"the new text\"}"
           "},\"required\":[\"handle\",\"text\"]}";
}

std::string AppSetTextTool::execute( const std::string &argumentsJson )
{
    if( !looksJsonObject( argumentsJson ) )
        return "app_set_text: arguments must be a JSON object";

    std::string handle, text;
    if( !handleArg( argumentsJson, handle ) )
        return "app_set_text: \"handle\" is required";
    if( !jsonStringArg( argumentsJson, "text", text ) )
        return "app_set_text: \"text\" is required";

    std::string error;
    if( !atui::platform::appSetWindowText( handle, text, error ) )
        return "app_set_text: " + error;
    return "app_set_text: window text set on " + handle;
}

std::string AppSetTextTool::approvalSummary( const std::string &argumentsJson ) const
{
    std::string handle;
    jsonStringArg( argumentsJson, "handle", handle );
    return std::string( "set text of " ) + handle;
}

std::string AppSetTextTool::callPreview( const std::string &argumentsJson ) const
{
    return approvalSummary( argumentsJson );
}

// ------------------------------------------------------------ send_msg

const char *AppSendMsgTool::name() const
{
    return "app_send_msg";
}

const char *AppSendMsgTool::description() const
{
    return "Expert door: post one message with value-only parameters "
           "(each \"0x..\" hex or decimal) to a window. For messages an "
           "app was written to answer -- its own commands, WM_CLOSE "
           "shapes, dialogs' conventions. WARNING, measured on live "
           "targets: WM_CHAR, WM_KEYUP and control messages that carry "
           "pointers can wedge or kill the target -- do not post them. "
           "If a normal tool covers the act, use the normal tool.";
}

const char *AppSendMsgTool::parametersSchema() const
{
    return "{\"type\":\"object\",\"properties\":{"
           "\"handle\":{\"type\":\"string\",\"description\":"
           "\"target window handle\"},"
           "\"message\":{\"type\":\"string\",\"description\":"
           "\"message number, 0x hex or decimal\"},"
           "\"wparam\":{\"type\":\"string\",\"description\":"
           "\"first value parameter (default 0)\"},"
           "\"lparam\":{\"type\":\"string\",\"description\":"
           "\"second value parameter (default 0)\"}"
           "},\"required\":[\"handle\",\"message\"]}";
}

std::string AppSendMsgTool::execute( const std::string &argumentsJson )
{
    if( !looksJsonObject( argumentsJson ) )
        return "app_send_msg: arguments must be a JSON object";

    std::string handle, msgText;
    if( !handleArg( argumentsJson, handle ) )
        return "app_send_msg: \"handle\" is required";
    if( !jsonStringArg( argumentsJson, "message", msgText ) )
        return "app_send_msg: \"message\" is required (a number string)";

    unsigned long msg = 0;
    if( !atui::platform::parseMessageWord( msgText, msg ) )
        return "app_send_msg: \"message\" is not a number";

    unsigned long mp1 = 0, mp2 = 0;
    std::string word;
    if( jsonStringArg( argumentsJson, "wparam", word )
        && !atui::platform::parseMessageWord( word, mp1 ) )
        return "app_send_msg: \"wparam\" is not a number";
    if( jsonStringArg( argumentsJson, "lparam", word )
        && !atui::platform::parseMessageWord( word, mp2 ) )
        return "app_send_msg: \"lparam\" is not a number";

    std::string error;
    if( !atui::platform::appPostMessage( handle, msg, mp1, mp2, error ) )
        return "app_send_msg: " + error;
    return "app_send_msg: message posted to " + handle
           + " (verify with the screenshot tool)";
}

std::string AppSendMsgTool::approvalSummary( const std::string &argumentsJson ) const
{
    std::string handle, msgText;
    jsonStringArg( argumentsJson, "handle", handle );
    jsonStringArg( argumentsJson, "message", msgText );
    return std::string( "post message " ) + msgText + " to " + handle;
}

std::string AppSendMsgTool::callPreview( const std::string &argumentsJson ) const
{
    return approvalSummary( argumentsJson );
}

AppCloseTool g_appCloseTool;
AppClickTool g_appClickTool;
AppSetTextTool g_appSetTextTool;
AppSendMsgTool g_appSendMsgTool;

} // namespace atui::tools
} // namespace atui
