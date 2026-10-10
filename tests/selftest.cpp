#include "harness.h"
#include "core/string.h"
#include "tui/theme.h"
#include "net/provider.h"
#include "net/json_extract.h"
#include "net/oai_client.h"
#include "tools/tool_registry.h"
#include "tools/tool.h"
#include "tools/time_tool.h"
#include "tools/json_args.h"
#include "tools/edit_file_tool.h"
#include "tools/run_command_tool.h"
#include "settings/ini.h"
#include "settings/settings.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace atui_test {

// Defined in tests/oai_mock_test.cpp (network builds only).
void init_network_tests();
// Defined in tests/oai_live_test.cpp (network builds only; registers nothing
// unless ATUI_LIVE_BASE_URL names a server).
void init_live_tests();
// Defined in tests/markdown_test.cpp (always built).
void init_markdown_tests();

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

// --- multiple providers -------------------------------------------------------

// Restores the provider store, the theme and $ATUI_CONFIG after a test that
// rewrites them (they are process-wide singletons).
class StoreGuard
{
public:
    StoreGuard()
        : list_( atui::net::providers() ),
          active_( atui::net::providerSettings().name ),
          theme_( atui::tui::currentTheme() )
    {
        const char *p = std::getenv( "ATUI_CONFIG" );
        if( p ) { hadConfig_ = true; savedConfig_ = p; }
    }
    ~StoreGuard()
    {
        atui::net::replaceProviders( list_, active_ );
        atui::tui::setTheme( theme_ );
        if( hadConfig_ ) setenv( "ATUI_CONFIG", savedConfig_.c_str(), 1 );
        else unsetenv( "ATUI_CONFIG" );
    }

private:
    std::vector<atui::net::ProviderSettings> list_;
    std::string active_;
    atui::tui::Theme theme_;
    bool hadConfig_ = false;
    std::string savedConfig_;
};

int test_provider_store()
{
    StoreGuard guard;

    atui::net::replaceProviders( std::vector<atui::net::ProviderSettings>(), "" );
    if( atui::net::providers().size() != 1 ) return -1;   // empty list ignored

    const std::size_t b = atui::net::addProvider( "beta" );
    const std::size_t c = atui::net::addProvider( "beta" );   // name is taken
    if( atui::net::providers().size() != 3 ) return -1;
    if( atui::net::providers()[b].name != "beta" ) return -1;
    if( atui::net::providers()[c].name != "beta-2" ) return -1;

    atui::net::setActiveProvider( b );
    if( atui::net::providerSettings().name != "beta" ) return -1;
    if( atui::net::activeProviderIndex() != b ) return -1;

    // Names match case-insensitively; unknown names change nothing.
    if( !atui::net::setActiveProviderByName( "BETA-2" ) ) return -1;
    if( atui::net::activeProviderIndex() != c ) return -1;
    if( atui::net::setActiveProviderByName( "nope" ) ) return -1;
    if( atui::net::activeProviderIndex() != c ) return -1;

    // Deleting the active provider keeps the store valid.
    if( !atui::net::removeProvider( c ) ) return -1;
    if( atui::net::providers().size() != 2 ) return -1;
    if( atui::net::activeProviderIndex() >= atui::net::providers().size() )
        return -1;

    // The last provider cannot be deleted.
    atui::net::setActiveProvider( 0 );
    if( !atui::net::removeProvider( 1 ) ) return -1;
    if( atui::net::removeProvider( 0 ) ) return -1;
    if( atui::net::providers().size() != 1 ) return -1;

    if( atui::net::findProvider( "default" ) != &atui::net::providers()[0] )
        return -1;
    if( atui::net::findProvider( "missing" ) != 0 ) return -1;
    return 0;
}

int test_provider_model_lists()
{
    atui::net::ProviderSettings p = atui::net::defaultProviderSettings();

    std::vector<std::string> ids;
    ids.push_back( "llama3.2" );
    ids.push_back( "qwen3:30b" );

    p.model = "qwen3:30b";
    atui::net::setModels( p, ids );
    if( p.model != "qwen3:30b" ) return -1;         // selection survives

    // A selection the endpoint does not report is kept (re-added in front)
    // rather than silently switching models.
    p.model = "gone";
    atui::net::setModels( p, ids );
    if( p.model != "gone" ) return -1;
    if( p.models.size() != 3 || p.models[0] != "gone" ) return -1;

    p.model = "";
    atui::net::setModels( p, ids );
    if( p.model != "llama3.2" ) return -1;          // defaults to the first

    atui::net::selectModel( p, "deepseek-r1" );     // appends unknown ids
    if( p.model != "deepseek-r1" ) return -1;
    if( p.models.size() != 3 ) return -1;

    const std::string text = atui::net::formatModelList( p.models );
    if( text != "llama3.2,qwen3:30b,deepseek-r1" ) return -1;
    if( atui::net::parseModelList( text ).size() != 3 ) return -1;
    if( atui::net::parseModelList( " a , ,b " ).size() != 2 ) return -1;
    if( atui::net::parseModelList( "" ).size() != 0 ) return -1;
    return 0;
}

int test_settings_provider_roundtrip()
{
    StoreGuard guard;
    const std::string path = tmpIniPath();
    std::remove( path.c_str() );
    setenv( "ATUI_CONFIG", path.c_str(), 1 );

    std::vector<atui::net::ProviderSettings> list;
    list.push_back( atui::net::defaultProviderSettings() );
    list[0].name = "local";
    list[0].baseUrl = "http://127.0.0.1:8080/v1";
    list[0].model = "llama";
    list[0].models = atui::net::parseModelList( "llama,phi" );
    list.push_back( atui::net::defaultProviderSettings() );
    list[1].name = "Hosted";
    list[1].baseUrl = "https://api.example.com/v1";
    list[1].apiKey = "secret";
    list[1].model = "gpt-x";
    list[1].models = atui::net::parseModelList( "gpt-x" );
    list[1].temperature = 0.25;
    atui::net::replaceProviders( list, "Hosted" );

    if( !atui::settings::saveAll() ) return -1;

    // The file holds one section per provider plus the active selection.
    {
        atui::settings::IniFile ini( path );
        if( !ini.load() ) return -1;
        if( ini.getString( "app", "provider" ) != "Hosted" ) return -1;
        if( ini.getString( "provider:local", "base_url" ) !=
            "http://127.0.0.1:8080/v1" ) return -1;
        if( ini.getString( "provider:hosted", "api_key" ) != "secret" )
            return -1;
        if( ini.getString( "provider:hosted", "name" ) != "Hosted" )
            return -1;   // display case preserved in the section
        if( ini.getString( "provider:local", "models" ) != "llama,phi" )
            return -1;
    }

    // Loading replaces the store, including which provider is active.
    atui::net::replaceProviders( std::vector<atui::net::ProviderSettings>(),
                                 "" );
    if( !atui::settings::loadAll() ) return -1;
    if( atui::net::providers().size() != 2 ) return -1;
    if( atui::net::providerSettings().name != "Hosted" ) return -1;
    if( atui::net::providerSettings().baseUrl !=
        "https://api.example.com/v1" ) return -1;
    if( atui::net::providerSettings().temperature != 0.25 ) return -1;
    if( atui::net::providerSettings().models.size() != 1 ) return -1;

    // Re-saving drops the section of a provider that was deleted (sections
    // come from the file in alphabetical order, so index 0 is "Hosted").
    atui::net::removeProvider( 0 );
    if( !atui::settings::saveAll() ) return -1;
    atui::settings::IniFile after( path );
    if( !after.load() ) return -1;
    if( after.has( "provider:hosted", "api_key" ) ) return -1;
    if( !after.has( "provider:local", "base_url" ) ) return -1;
    if( atui::net::providers().size() != 1 ) return -1;
    return 0;
}

// A config file from before multiple providers: the single [provider] section
// becomes the provider named "default".
int test_settings_legacy_provider_section()
{
    StoreGuard guard;
    const std::string path = tmpIniPath();
    setenv( "ATUI_CONFIG", path.c_str(), 1 );
    writeFile( path,
        "[provider]\n"
        "base_url = http://legacy:1234/v1\n"
        "model = old-model\n"
        "temperature = 0.9\n"
        "[ui]\n"
        "theme = classic\n" );

    if( !atui::settings::loadAll() ) return -1;
    if( atui::net::providers().size() != 1 ) return -1;
    if( atui::net::providerSettings().name != "default" ) return -1;
    if( atui::net::providerSettings().baseUrl != "http://legacy:1234/v1" )
        return -1;
    if( atui::net::providerSettings().model != "old-model" ) return -1;
    if( atui::net::providerSettings().temperature != 0.9 ) return -1;
    if( atui::tui::currentTheme() != atui::tui::ThemeClassic ) return -1;

    // The selected model is offered even though no model list was stored.
    if( atui::net::providerSettings().models.size() != 1 ||
        atui::net::providerSettings().models[0] != "old-model" ) return -1;
    return 0;
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
    // The file-write and shell tools are registered and require approval.
    atui::tools::Tool *ed = reg.find( "edit_file" );
    atui::tools::Tool *rc = reg.find( "run_command" );
    if( !ed || !rc )
        return -1;
    if( reg.find( "get_time" )->requiresApproval() )
        return -1;
    if( !ed->requiresApproval() || !rc->requiresApproval() )
        return -1;
    std::string json = reg.renderToolsJson();
    if( json.find( "\"type\":\"function\"" ) == std::string::npos )
        return -1;
    if( json.find( "get_time" ) == std::string::npos )
        return -1;
    if( json.find( "edit_file" ) == std::string::npos )
        return -1;
    if( json.find( "run_command" ) == std::string::npos )
        return -1;
    if( json[0] != '[' || json[json.size() - 1] != ']' )
        return -1;
    return 0;
}

// --- json argument reader -----------------------------------------------------

int test_json_args_strings()
{
    std::string obj = "{\"path\":\"a/b.txt\",\"note\":\"path\",\"x\":\"q\\\"r\"}";
    std::string v;
    if( !atui::tools::jsonStringArg( obj, "path", v ) || v != "a/b.txt" )
        return -1;
    // A key must not match a value that happens to equal the key name.
    if( atui::tools::jsonStringArg( obj, "path2", v ) )
        return -2;
    // Escaped quote decoded.
    if( !atui::tools::jsonStringArg( obj, "x", v ) || v != "q\"r" )
        return -3;
    // \n and \t decoded to real control chars.
    if( !atui::tools::jsonStringArg( "{\"old_text\":\"a\\nb\\tc\"}", "old_text", v )
        || v != "a\nb\tc" )
        return -4;
    // Braces inside a string must not confuse the scan.
    if( !atui::tools::jsonStringArg( "{\"command\":\"echo } {\",\"y\":1}", "command", v )
        || v != "echo } {" )
        return -5;
    if( atui::tools::jsonStringArg( obj, "missing", v ) )
        return -6;
    return 0;
}

int test_json_args_bool_and_nested()
{
    bool b = false;
    if( !atui::tools::jsonBoolArg( "{\"all\":true}", "all", b ) || b != true )
        return -1;
    b = true;
    if( !atui::tools::jsonBoolArg( "{\"all\":false}", "all", b ) || b != false )
        return -2;
    if( !atui::tools::jsonBoolArg( "{\"all\":\"true\"}", "all", b ) || b != true )
        return -3;
    if( atui::tools::jsonBoolArg( "{\"all\":5}", "all", b ) )
        return -4;   // not a boolean value

    // A top-level string key wins even when a nested object holds the same key.
    std::string v;
    if( !atui::tools::jsonStringArg( "{\"a\":{\"path\":\"x\"},\"path\":\"top\"}",
                                     "path", v ) || v != "top" )
        return -5;
    // A key that only exists nested is not a top-level string -> not found.
    v.clear();
    if( atui::tools::jsonStringArg( "{\"a\":{\"path\":\"x\"}}", "path", v ) )
        return -6;
    return 0;
}

int test_json_args_looks_object()
{
    if( !atui::tools::looksJsonObject( " {\"a\":1} " ) )
        return -1;
    if( atui::tools::looksJsonObject( "[1,2]" ) )
        return -2;
    if( atui::tools::looksJsonObject( "garbage" ) )
        return -3;
    if( atui::tools::looksJsonObject( "" ) )
        return -4;
    return 0;
}

// --- edit_file ----------------------------------------------------------------

std::string toolTmpPath( const char *name )
{
    const char *d = std::getenv( "TMPDIR" );
    if( !d || !*d ) d = "/tmp";
    std::string s( d );
    if( s.empty() || s[s.size() - 1] != '/' ) s += "/";
    s += "atui_";
    s += name;
    return s;
}

void toolWriteFile( const std::string &path, const std::string &content )
{
    std::ofstream o( path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc );
    o.write( content.data(), (std::streamsize) content.size() );
}

std::string toolReadFile( const std::string &path )
{
    std::ifstream in( path.c_str(), std::ios::in | std::ios::binary );
    std::string s( (std::istreambuf_iterator<char>( in )),
                    std::istreambuf_iterator<char>() );
    return s;
}

int test_edit_file_first_and_all()
{
    std::string p = toolTmpPath( "edit_first.txt" );
    toolWriteFile( p, "x x x and hello world\n" );

    // First occurrence only.
    atui::tools::EditFileTool t;
    std::string r = t.execute( "{\"path\":\"" + p + "\",\"old_text\":\"x\",\"new_text\":\"y\"}" );
    std::string got = toolReadFile( p );
    if( got != "y x x and hello world\n" )
        return -1;
    if( r.find( "replaced 1" ) == std::string::npos )
        return -2;

    // all=true replaces every occurrence.
    t.execute( "{\"path\":\"" + p + "\",\"old_text\":\"x\",\"new_text\":\"z\",\"all\":true}" );
    got = toolReadFile( p );
    if( got != "y z z and hello world\n" )
        return -3;

    // Multiline exact match with real newlines passed through JSON escapes.
    std::string p2 = toolTmpPath( "edit_multi.txt" );
    toolWriteFile( p2, "one\ntwo\nthree\n" );
    t.execute( "{\"path\":\"" + p2 + "\",\"old_text\":\"two\\nthree\",\"new_text\":\"2/3\"}" );
    if( toolReadFile( p2 ) != "one\n2/3\n" )
        return -4;

    std::remove( p.c_str() );
    std::remove( p2.c_str() );
    return 0;
}

int test_edit_file_errors()
{
    atui::tools::EditFileTool t;
    std::string p = toolTmpPath( "edit_miss.txt" );
    toolWriteFile( p, "abc\n" );

    // old_text not found: file unchanged, message mentions not found.
    std::string r = t.execute( "{\"path\":\"" + p + "\",\"old_text\":\"zzz\",\"new_text\":\"q\"}" );
    if( r.find( "not found" ) == std::string::npos )
        return -1;
    if( toolReadFile( p ) != "abc\n" )
        return -2;

    // Empty old_text rejected.
    r = t.execute( "{\"path\":\"" + p + "\",\"old_text\":\"\",\"new_text\":\"q\"}" );
    if( r.find( "empty" ) == std::string::npos )
        return -3;

    // Missing file.
    r = t.execute( "{\"path\":\"/nonexistent/atui/nope.txt\",\"old_text\":\"a\",\"new_text\":\"b\"}" );
    if( r.find( "cannot read" ) == std::string::npos )
        return -4;

    // Non-object arguments.
    r = t.execute( "not json" );
    if( r.find( "JSON object" ) == std::string::npos )
        return -5;

    std::remove( p.c_str() );
    return 0;
}

// --- run_command --------------------------------------------------------------

bool containsSub( const std::string &h, const std::string &n )
{
    return h.find( n ) != std::string::npos;
}

int test_run_command_basic()
{
    atui::tools::RunCommandTool t;
    std::string r = t.execute( "{\"command\":\"echo hello\"}" );
    if( !containsSub( r, "exit code 0" ) )
        return -1;
    if( !containsSub( r, "hello" ) )
        return -2;

    // Non-zero exit code is reported.
    r = t.execute( "{\"command\":\"exit 3\"}" );
    if( !containsSub( r, "exit code 3" ) )
        return -3;

    // stderr is merged into the captured output.
    r = t.execute( "{\"command\":\"echo boom 1>&2\"}" );
    if( !containsSub( r, "boom" ) )
        return -4;

    // Empty command rejected.
    r = t.execute( "{\"command\":\"\"}" );
    if( r.find( "required" ) == std::string::npos )
        return -5;
    return 0;
}

// --- call previews (what the transcript keeps of a tool call) -----------------

int test_tool_call_previews()
{
    // A command reads like the shell line that was run.
    atui::tools::RunCommandTool rc;
    if( rc.callPreview( "{\"command\":\"make test\"}" ) != "make test" )
        return -1;

    // Multi-line commands keep their line breaks; a trailing newline does not
    // add an empty row.
    if( rc.callPreview( "{\"command\":\"cd build\\nmake -j4\\n\"}" )
        != "cd build\nmake -j4" )
        return -2;

    // Control bytes never reach the screen as themselves.
    if( rc.callPreview( "{\"command\":\"cd\\tbuild\"}" ) != "cd build" )
        return -3;
    if( rc.callPreview( "{\"command\":\"a\\u0001b\"}" ) != "a?b" )
        return -4;

    // An over-long argument is clipped instead of flooding the transcript.
    std::string big( 2048, 'x' );
    std::string clipped = rc.callPreview( "{\"command\":\"" + big + "\"}" );
    if( clipped.size() <= 1000 )
        return -5;
    if( clipped.compare( clipped.size() - 3, 3, "..." ) != 0 )
        return -6;

    // Nothing usable to show still names the tool.
    if( rc.callPreview( "{}" ).find( "run_command" ) == std::string::npos )
        return -7;

    // An edit names the file, the mode and both sides of the replacement.
    atui::tools::EditFileTool ed;
    std::string p = ed.callPreview( "{\"path\":\"src/main.cpp\","
        "\"old_text\":\"retrun 1\",\"new_text\":\"return 1\"}" );
    if( !containsSub( p, "edit_file \"src/main.cpp\"" ) )
        return -8;
    if( !containsSub( p, "(first match)" ) )
        return -9;
    if( !containsSub( p, "- old: \"retrun 1\"" ) )
        return -10;
    if( !containsSub( p, "+ new: \"return 1\"" ) )
        return -11;
    p = ed.callPreview( "{\"path\":\"a.c\",\"old_text\":\"x\","
                        "\"new_text\":\"y\",\"all\":true}" );
    if( !containsSub( p, "(all matches)" ) )
        return -12;
    // Newlines inside the replaced text stay escaped, so the record is one line
    // per side rather than a wall of text.
    p = ed.callPreview( "{\"path\":\"a.c\",\"old_text\":\"a\\nb\",\"new_text\":\"c\"}" );
    if( !containsSub( p, "- old: \"a\\nb\"" ) )
        return -13;

    // A tool with nothing specific to show falls back to its name.
    atui::tools::TimeTool tm;
    if( tm.callPreview( "{}" ) != "get_time" )
        return -14;
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
    register_test( "provider: store add/switch/delete", &test_provider_store );
    register_test( "provider: model lists", &test_provider_model_lists );
    register_test( "settings: provider round-trip",
                   &test_settings_provider_roundtrip );
    register_test( "settings: legacy [provider] section",
                   &test_settings_legacy_provider_section );
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
    register_test( "tool: json args strings", &test_json_args_strings );
    register_test( "tool: json args bool/nested", &test_json_args_bool_and_nested );
    register_test( "tool: json args looks object", &test_json_args_looks_object );
    register_test( "tool: edit_file first/all", &test_edit_file_first_and_all );
    register_test( "tool: edit_file errors", &test_edit_file_errors );
    register_test( "tool: run_command basic", &test_run_command_basic );
    register_test( "tool: call previews", &test_tool_call_previews );

    // Markdown + syntax highlighting (network-independent).
    init_markdown_tests();

#ifdef ATUI_HAVE_NETWORK
    // Real-server tests, last: they retarget the process-wide provider, and
    // they register nothing at all unless ATUI_LIVE_BASE_URL is set.
    init_live_tests();
#endif
}

} // namespace atui_test
