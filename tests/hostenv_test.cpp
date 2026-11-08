// Tests for the host environment brief -- the sentence that tells the
// model where it is. They run everywhere (the point of the module is
// that its facts are compile-time), and what they assert is the
// contract with the system message: it names the host, it is short
// enough to ride every request, and on the hosts that need warnings
// the warnings are in it.
#include "harness.h"
#include "core/hostenv.h"

#include <string>

namespace atui_test {

namespace {

int test_os_name_present()
{
    const std::string name( atui::core::hostOsName() );
    if( name.empty() )
        return -1;
    return 0;
}

int test_brief_names_the_host()
{
    const std::string brief = atui::core::hostEnvBrief();
    if( brief.find( atui::core::hostOsName() ) == std::string::npos )
        return -1;
    return 0;
}

int test_brief_bounded()
{
    // The brief rides every request of every turn: a paragraph is
    // paid for each time, so there is a budget. 1600 bytes is well
    // past any honest brief and well under a page of tokens.
    if( atui::core::hostEnvBrief().size() > 1600 )
        return -1;
    return 0;
}

int test_posix_flag_is_the_platform()
{
#if defined( __unix__ ) || defined( __APPLE__ )
    if( !atui::core::hostIsPosix() )
        return -1;
#else
    if( atui::core::hostIsPosix() )
        return -2;
#endif
    return 0;
}

int test_brief_matches_the_shell_it_ships()
{
    // The brief must describe the shell run_command actually uses --
    // per platform, asserted where the test runs.
    const std::string brief = atui::core::hostEnvBrief();
#if defined( __OS2__ )
    // Names the real shell, the real traps, the real path shape.
    if( brief.find( "cmd.exe" ) == std::string::npos )
        return -1;
    if( brief.find( "NOT" ) == std::string::npos )
        return -2;
    if( brief.find( "ls," ) == std::string::npos )
        return -3;
    if( brief.find( "C:\\OS2" ) == std::string::npos )
        return -4;
    // And it must not invite the Unix habit it exists to prevent.
    if( brief.find( "POSIX idioms" ) != std::string::npos )
        return -5;
#elif defined( __unix__ ) || defined( __APPLE__ )
    if( brief.find( "/bin/sh" ) == std::string::npos )
        return -6;
#endif
    return 0;
}

int test_system_message_assembly()
{
    // The singleton is app state; the test borrows it and puts it
    // back, same discipline every front follows.
    atui::core::AgentPrompts &p = atui::core::agentPrompts();
    const bool briefWas = p.hostBrief;
    const std::vector<std::string> extraWas = p.extra;
    int rc = 0;

    // Brief wanted, nothing added: the message is the brief and no
    // "User instructions:" heading shows for an empty block.
    p.hostBrief = true;
    p.extra.clear();
    std::string msg = atui::core::buildSystemMessage();
    if( msg.find( atui::core::hostOsName() ) == std::string::npos )
        rc = -1;
    if( msg.find( "User instructions" ) != std::string::npos )
        rc = -2;

    // Extras ride under a heading that names their author, in order,
    // and blank editor lines are air.
    if( !rc )
    {
        p.extra.push_back( "Answer tersely" );
        p.extra.push_back( "" );
        p.extra.push_back( "Say no when unsure" );
        msg = atui::core::buildSystemMessage();
        if( msg.find( "User instructions:" ) == std::string::npos )
            rc = -3;
        if( msg.find( "Answer tersely\nSay no when unsure" )
            == std::string::npos )
            rc = -4;
        if( msg.find( atui::core::hostOsName() )
            == std::string::npos )
            rc = -5;
    }

    // Brief vetoed: the user's words stand alone...
    if( !rc )
    {
        p.hostBrief = false;
        msg = atui::core::buildSystemMessage();
        if( msg != "User instructions:\nAnswer tersely\n"
                   "Say no when unsure" )
            rc = -6;
    }

    // ...and silence on both knobs really is silence: chat_turn
    // sends no system message for it rather than an empty one.
    if( !rc )
    {
        p.extra.clear();
        if( !atui::core::buildSystemMessage().empty() )
            rc = -7;
    }

    p.hostBrief = briefWas;
    p.extra = extraWas;
    return rc;
}

} // anonymous namespace

void init_hostenv_tests()
{
    register_test( "hostenv_os_name_present", test_os_name_present );
    register_test( "hostenv_brief_names_the_host",
                   test_brief_names_the_host );
    register_test( "hostenv_brief_bounded", test_brief_bounded );
    register_test( "hostenv_posix_flag_is_the_platform",
                   test_posix_flag_is_the_platform );
    register_test( "hostenv_brief_matches_the_shell_it_ships",
                   test_brief_matches_the_shell_it_ships );
    register_test( "hostenv_system_message_assembly",
                   test_system_message_assembly );
}

} // namespace atui_test
