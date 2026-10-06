#include "harness.h"
#include "core/string.h"
#include "tui/theme.h"
#include "net/provider.h"
#include "net/json_extract.h"
#include "net/oai_client.h"
#include "tools/tool_registry.h"
#include "tools/time_tool.h"
#include "settings/ini.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace atui_test {

// Defined in tests/oai_mock_test.cpp (network builds only).
void init_network_tests();

namespace {

int test_string_empty()
{
    atui::core::String s;
    if( s.size() != 0 )
        return -1;
    if( s.c_str() == 0 || std::strcmp( s.c_str(), "" ) != 0 )
        return -1;
    if( s.compareTo( atui::core::String() ) != 0 )
        return -1;
    if( !s.equals( "" ) )
        return -1;
    return 0;
}

int test_string_append()
{
    atui::core::String s;
    s.append( "atui" );
    s.append( " " );
    s.append( "selftest" );
    if( s.size() != 13 )
        return -1;
    if( std::strcmp( s.c_str(), "atui selftest" ) != 0 )
        return -1;
    return 0;
}

int test_string_operator_plus_equals()
{
    atui::core::String s( "hello" );
    s += " world";
    if( std::strcmp( s.c_str(), "hello world" ) != 0 )
        return -1;
    atui::core::String a;
    atui::core::String b( "x" );
    a += b;
    if( a.size() != 1 || std::strcmp( a.c_str(), "x" ) != 0 )
        return -1;
    return 0;
}

int test_string_copy_assign()
{
    atui::core::String s( "copy me" );
    atui::core::String t( s );
    if( t.size() != 7 )
        return -1;
    if( std::strcmp( t.c_str(), "copy me" ) != 0 )
        return -1;

    atui::core::String u;
    u = s;
    if( u.size() != 7 )
        return -1;
    if( std::strcmp( u.c_str(), "copy me" ) != 0 )
        return -1;

    // independent storage after modifying t
    atui::core::String v = s;
    v.clear();
    if( s.size() != 7 || std::strcmp( s.c_str(), "copy me" ) != 0 )
        return -1;
    return 0;
}

int test_string_truncate()
{
    atui::core::String s( "0123456789" );
    atui::core::String sub = s.substr( 2 );
    if( sub.size() != 8 || std::strcmp( sub.c_str(), "23456789" ) != 0 )
        return -1;

    atui::core::String sub2 = s.substr( 2, 5 );
    if( sub2.size() != 3 || std::strcmp( sub2.c_str(), "234" ) != 0 )
        return -1;

    atui::core::String sub3 = s.substr( 0, 0 );
    if( sub3.size() != 0 )
        return -1;

    atui::core::String sub4 = s.substr( 15 );
    if( sub4.size() != 0 )
        return -1;
    return 0;
}

int test_string_indexOf()
{
    atui::core::String s( "hello world" );
    if( s.indexOf( "world" ) != 6 )
        return -1;
    if( s.indexOf( "hello" ) != 0 )
        return -1;
    if( s.indexOf( "world", 1 ) != 6 )
        return -1;
    if( s.indexOf( "world", 10 ) != -1 )
        return -1;
    if( s.indexOf( "xyz" ) != -1 )
        return -1;
    if( s.indexOf( "" ) != 0 )
        return -1;
    return 0;
}

int test_string_compareTo()
{
    atui::core::String a( "abc" );
    atui::core::String b( "abc" );
    atui::core::String c( "abd" );
    atui::core::String d( "ab" );
    if( a.compareTo( b ) != 0 )
        return -1;
    if( a.compareTo( c ) > 0 )
        return -1;
    // "abc" > "ab" (d is a prefix of a)
    if( a.compareTo( d ) < 0 )
        return -1;
    if( d.compareTo( c ) > 0 )
        return -1;
    return 0;
}

int test_theme_default_dark()
{
    // Dark is the default when ATUI_THEME is unset; an explicit
    // ATUI_THEME=classic still selects the stock palette. This test must
    // run before any test that calls setTheme(), since the theme is a
    // lazily-initialized process singleton.
    const char *env = ::getenv( "ATUI_THEME" );
    bool wantClassic = env && std::strcmp( env, "classic" ) == 0;
    if( atui::tui::currentTheme() != (wantClassic ? atui::tui::ThemeClassic
                                                  : atui::tui::ThemeDark) )
        return -1;
    if( std::strcmp( atui::tui::themeName( atui::tui::ThemeClassic ), "classic" ) != 0 )
        return -1;
    if( std::strcmp( atui::tui::themeName( atui::tui::ThemeDark ), "dark" ) != 0 )
        return -1;
    return 0;
}

int test_theme_palette_layout()
{
    // Both palettes keep the classic Turbo Vision layout: 135 entries.
    atui::tui::setTheme( atui::tui::ThemeClassic );
    TPalette& classic = atui::tui::themePalette();
    if( classic[0] != 135 )
        return -1;

    atui::tui::setTheme( atui::tui::ThemeDark );
    TPalette& dark = atui::tui::themePalette();
    if( dark[0] != 135 )
        return -1;
    return 0;
}

int test_theme_dark_recolor()
{
    // The dark palette must differ from the classic one, and entry 1
    // (TBackground) must keep its roles: blue-on-gray becomes light-blue
    // on dark slate, expressed as RGB colors.
    atui::tui::setTheme( atui::tui::ThemeClassic );
    TPalette& classic = atui::tui::themePalette();
    atui::tui::setTheme( atui::tui::ThemeDark );
    TPalette& dark = atui::tui::themePalette();

    int diff = 0;
    for( int i = 1; i <= 135; ++i )
        if( classic[i] != dark[i] )
            ++diff;
    if( diff < 100 )
        return -1;

    TColorAttr e1 = dark[1];
    if( !e1.getForeground().isRGB() || !e1.getBackground().isRGB() )
        return -1;
    if( e1.getForeground().asRGB() != 0x7FA8DC )
        return -1;
    if( e1.getBackground().asRGB() != 0x252B33 )
        return -1;

    // Entry 2 (menu bar): classic black-on-gray becomes near-white on slate.
    TColorAttr e2 = dark[2];
    if( e2.getForeground().asRGB() != 0xE8EAED )
        return -1;
    if( e2.getBackground().asRGB() != 0x252B33 )
        return -1;

    atui::tui::setTheme( atui::tui::ThemeClassic );
    return 0;
}

int test_theme_switch()
{
    if( !atui::tui::setTheme( atui::tui::ThemeDark ) )
        return -1;
    if( atui::tui::currentTheme() != atui::tui::ThemeDark )
        return -1;
    // Switching to the same theme reports no change.
    if( atui::tui::setTheme( atui::tui::ThemeDark ) )
        return -1;
    if( !atui::tui::setTheme( atui::tui::ThemeClassic ) )
        return -1;
    if( atui::tui::currentTheme() != atui::tui::ThemeClassic )
        return -1;
    return 0;
}

int test_provider_singleton_mutable()
{
    // providerSettings() is one process-wide instance the Providers dialog
    // edits; mutations must be visible on the next read (per-request read
    // in OaiChatModel relies on this).
    atui::net::ProviderSettings &a = atui::net::providerSettings();
    atui::net::ProviderSettings &b = atui::net::providerSettings();
    if( &a != &b )
        return -1;

    std::string keepUrl = a.baseUrl;
    std::string keepModel = a.model;
    double keepTemp = a.temperature;

    a.baseUrl = "http://192.168.0.50:8081/v1";
    a.model = "qwen";
    a.temperature = 0.42;

    if( atui::net::providerSettings().baseUrl != "http://192.168.0.50:8081/v1" )
        return -1;
    if( atui::net::providerSettings().model != "qwen" )
        return -1;
    if( atui::net::providerSettings().temperature != 0.42 )
        return -1;

    // Restore so other runs/tests are unaffected.
    a.baseUrl = keepUrl;
    a.model = keepModel;
    a.temperature = keepTemp;
    return 0;
}

// --- INI helper ---------------------------------------------------------------

std::string tmpIniPath()
{
    const char *d = std::getenv( "TMPDIR" );
    if( !d || !*d ) d = std::getenv( "TEMP" );
    if( !d || !*d ) d = std::getenv( "TMP" );
    if( !d || !*d ) d = "/tmp";
    std::string s( d );
    if( s.empty() || ( s[s.size() - 1] != '/' && s[s.size() - 1] != '\\' ) )
        s += "/";
    s += "atui_ini_selftest.ini";
    return s;
}

void writeFile( const std::string &path, const char *content )
{
    std::ofstream out( path.c_str(), std::ios::out | std::ios::trunc );
    out << content;
}

int test_ini_parse()
{
    std::string path = tmpIniPath();
    writeFile( path,
        "# leading comment\n"
        "; another comment\n"
        "[provider]\n"
        "base_url = http://127.0.0.1:8080/v1\n"
        "MODEL=llama\n"          // keys are case-insensitive
        "temperature = 0.42\n"
        "\n"
        "[ui]\n"
        "theme = dark\n" );

    atui::settings::IniFile ini( path );
    if( !ini.load() )
        return -1;
    if( ini.getString( "provider", "base_url" ) != "http://127.0.0.1:8080/v1" )
        return -1;
    // case-insensitive key + section lookups
    if( ini.getString( "PROVIDER", "model" ) != "llama" )
        return -1;
    if( ini.getDouble( "provider", "temperature", 0.0 ) != 0.42 )
        return -1;
    if( ini.getString( "ui", "theme" ) != "dark" )
        return -1;
    if( !ini.has( "ui", "THEME" ) )
        return -1;
    if( ini.has( "ui", "missing" ) )
        return -1;
    std::remove( path.c_str() );
    return 0;
}

int test_ini_typed_defaults()
{
    std::string path = tmpIniPath();
    writeFile( path,
        "[s]\n"
        "i = 42\n"
        "bad_i = notanumber\n"
        "b_yes = yes\n"
        "b_off = off\n" );

    atui::settings::IniFile ini( path );
    if( !ini.load() )
        return -1;
    if( ini.getInt( "s", "i", -1 ) != 42 )
        return -1;
    if( ini.getInt( "s", "bad_i", -7 ) != -7 )   // unparseable -> default
        return -1;
    if( ini.getInt( "s", "absent", 99 ) != 99 )  // missing -> default
        return -1;
    if( ini.getBool( "s", "b_yes", false ) != true )
        return -1;
    if( ini.getBool( "s", "b_off", true ) != false )
        return -1;
    if( ini.getBool( "s", "absent", true ) != true )
        return -1;
    if( ini.getDouble( "s", "absent", 1.5 ) != 1.5 )
        return -1;
    std::remove( path.c_str() );
    return 0;
}

int test_ini_save_roundtrip()
{
    std::string path = tmpIniPath();
    std::remove( path.c_str() );

    {
        atui::settings::IniFile ini( path );
        ini.setString( "provider", "base_url", "http://example:1/v1" );
        ini.setString( "provider", "model", "qwen" );
        ini.setDouble( "provider", "temperature", 0.7 );
        ini.setString( "ui", "theme", "dark" );
        if( !ini.save() )
            return -1;
    }

    atui::settings::IniFile in( path );
    if( !in.load() )
        return -1;
    if( in.getString( "provider", "base_url" ) != "http://example:1/v1" )
        return -1;
    if( in.getString( "provider", "model" ) != "qwen" )
        return -1;
    if( in.getDouble( "provider", "temperature", 0.0 ) != 0.7 )
        return -1;
    if( in.getString( "ui", "theme" ) != "dark" )
        return -1;
    std::remove( path.c_str() );
    return 0;
}

int test_ini_load_missing_file()
{
    std::string path = tmpIniPath();
    std::remove( path.c_str() );   // ensure it does not exist
    atui::settings::IniFile ini( path );
    if( ini.load() )               // missing file must report false, not crash
        return -1;
    if( ini.loaded() )
        return -1;
    // defaults still returned
    if( ini.getString( "provider", "base_url", "fallback" ) != "fallback" )
        return -1;
    return 0;
}

// --- tool calling -------------------------------------------------------------

// finish_reason / delta.tool_calls parsing lives in json_extract.cpp, which is
// only compiled in network builds.
#ifdef ATUI_HAVE_NETWORK
int test_finish_reason()
{
    std::string a = "{\"choices\":[{\"index\":0,\"delta\":{},"
                    "\"finish_reason\":\"tool_calls\"}]}";
    if( atui::net::finishReason( a ) != "tool_calls" )
        return -1;
    std::string b = "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"x\"},"
                    "\"finish_reason\":\"stop\"}]}";
    if( atui::net::finishReason( b ) != "stop" )
        return -1;
    std::string nul = "{\"choices\":[{\"index\":0,\"delta\":{},"
                      "\"finish_reason\":null}]}";
    if( atui::net::finishReason( nul ) != "" )
        return -1;
    if( atui::net::finishReason( "{}" ) != "" )
        return -1;
    return 0;
}

// Simulate the client's index-merge over streaming fragments.
int test_tool_call_frags_split()
{
    // First chunk: id + name + empty arguments start.
    std::string c1 =
        "{\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\","
        "\"content\":null,\"tool_calls\":[{\"index\":0,\"id\":\"call_1\","
        "\"type\":\"function\",\"function\":{\"name\":\"get_time\","
        "\"arguments\":\"\"}}]}}]}";
    // Second chunk: a fragment of the arguments string.
    std::string c2 =
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":"
        "[{\"index\":0,\"function\":{\"arguments\":\"{}\"}}]}}]}";

    std::vector<atui::net::OaiToolCall> acc;  // merge like the client
    std::vector<atui::net::ToolCallFrag> f1;
    if( !atui::net::parseToolCallFrags( c1, f1 ) || f1.size() != 1 )
        return -1;
    if( f1[0].id != "call_1" || f1[0].name != "get_time" )
        return -1;
    if( f1[0].index != 0 )
        return -1;

    // merge f1
    acc.push_back( atui::net::OaiToolCall() );
    acc[0].id = f1[0].id;
    acc[0].name = f1[0].name;
    acc[0].arguments += f1[0].arguments;

    std::vector<atui::net::ToolCallFrag> f2;
    if( !atui::net::parseToolCallFrags( c2, f2 ) || f2.size() != 1 )
        return -1;
    acc[0].arguments += f2[0].arguments;

    if( acc[0].arguments != "{}" )
        return -1;
    return 0;
}

// Arguments may themselves be JSON containing braces and escaped quotes; the
// scanner must not be confused by them.
int test_tool_call_args_with_braces()
{
    std::string chunk =
        "{\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{"
        "\"index\":0,\"function\":{\"name\":\"x\","
        "\"arguments\":\"{\\\"a\\\": 1, \\\"b\\\": \\\"q\\\"}\"}"
        "}]}}]}";
    std::vector<atui::net::ToolCallFrag> frags;
    if( !atui::net::parseToolCallFrags( chunk, frags ) || frags.size() != 1 )
        return -1;
    if( frags[0].name != "x" )
        return -1;
    // Decoded arguments must equal the raw JSON text (brace/quote balanced).
    if( frags[0].arguments != "{\"a\": 1, \"b\": \"q\"}" )
        return -1;
    return 0;
}
#endif // ATUI_HAVE_NETWORK

int test_time_tool()
{
    atui::tools::TimeTool t;
    if( std::string( t.name() ) != "get_time" )
        return -1;
    std::string r = t.execute( "{}" );
    // Expect an ISO-style "YYYY-MM-DD HH:MM:SS" prefix (>= 19 chars) with
    // digits and separators.
    if( r.size() < 19 )
        return -1;
    if( r.find( '-' ) == std::string::npos || r.find( ':' ) == std::string::npos )
        return -1;
    bool hasDigit = false;
    for( std::size_t i = 0; i < r.size(); ++i )
        if( r[i] >= '0' && r[i] <= '9' ) { hasDigit = true; break; }
    if( !hasDigit )
        return -1;
    return 0;
}

int test_tool_registry()
{
    atui::tools::registerDefaults();
    atui::tools::ToolRegistry &reg = atui::tools::ToolRegistry::instance();
    if( !reg.find( "get_time" ) )
        return -1;
    if( reg.find( "does_not_exist" ) )
        return -1;
    std::string json = reg.renderToolsJson();
    if( json.find( "\"type\":\"function\"" ) == std::string::npos )
        return -1;
    if( json.find( "get_time" ) == std::string::npos )
        return -1;
    if( json[0] != '[' || json[json.size() - 1] != ']' )
        return -1;
    return 0;
}

} // namespace

void init()
{
    register_test( "string: empty", &test_string_empty );
    register_test( "string: append", &test_string_append );
    register_test( "string: +=", &test_string_operator_plus_equals );
    register_test( "string: copy/assign", &test_string_copy_assign );
    register_test( "string: substr", &test_string_truncate );
    register_test( "string: indexOf", &test_string_indexOf );
    register_test( "string: compareTo", &test_string_compareTo );
    register_test( "theme: default dark", &test_theme_default_dark );
    register_test( "theme: palette layout", &test_theme_palette_layout );
    register_test( "theme: dark recolor", &test_theme_dark_recolor );
    register_test( "theme: switch", &test_theme_switch );
    register_test( "provider: singleton mutable", &test_provider_singleton_mutable );
    register_test( "ini: parse", &test_ini_parse );
    register_test( "ini: typed defaults", &test_ini_typed_defaults );
    register_test( "ini: save round-trip", &test_ini_save_roundtrip );
    register_test( "ini: load missing file", &test_ini_load_missing_file );
#ifdef ATUI_HAVE_NETWORK
    register_test( "finish_reason: parse", &test_finish_reason );
    register_test( "tool: call frags split", &test_tool_call_frags_split );
    register_test( "tool: args with braces", &test_tool_call_args_with_braces );
    // Socket-backed OpenAI client + chat-model integration tests (defined in
    // tests/oai_mock_test.cpp).
    init_network_tests();
#endif
    register_test( "tool: get_time", &test_time_tool );
    register_test( "tool: registry render", &test_tool_registry );
}

} // namespace atui_test
