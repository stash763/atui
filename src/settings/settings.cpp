// Implementation of the application settings glue. See settings.h.
#include "settings.h"

#include "ini.h"
#include "net/provider.h"
#include "tui/theme.h"

#include <cstdlib>
#include <string>

namespace atui {
namespace settings {

namespace {

const char *envOrNone( const char *name )
{
    const char *v = std::getenv( name );
    return ( v && v[0] ) ? v : 0;
}

// One [provider:<id>] section -> settings. Keys that are absent fall back to
// environment/default values; a provider with no stored model list still offers
// its selected model.
net::ProviderSettings readProvider( const IniFile &ini,
                                    const std::string &section,
                                    const std::string &defaultName )
{
    net::ProviderSettings p = net::defaultProviderSettings();
    p.name  = ini.getString( section.c_str(), "name", defaultName );
    p.baseUrl = ini.getString( section.c_str(), "base_url", p.baseUrl );
    p.apiKey  = ini.getString( section.c_str(), "api_key",  p.apiKey );
    p.model   = ini.getString( section.c_str(), "model",    p.model );
    p.temperature = ini.getDouble( section.c_str(), "temperature",
                                   p.temperature );
    p.thinkingResend = ini.getBool( section.c_str(), "thinking_resend",
                                    p.thinkingResend );
    p.models = net::parseModelList(
        ini.getString( section.c_str(), "models", std::string() ) );
    if( p.models.empty() ) p.models.push_back( p.model );
    return p;
}

} // anonymous namespace

std::string configPath()
{
    const char *p = envOrNone( "ATUI_CONFIG" );
    if( p ) return std::string( p );

    const char *xdg = envOrNone( "XDG_CONFIG_HOME" );
    if( xdg ) return std::string( xdg ) + "/atui/config.ini";

    const char *home = envOrNone( "HOME" );
    if( home ) return std::string( home ) + "/.atui.ini";

    return "atui.ini";
}

bool loadAll()
{
    IniFile ini( configPath() );
    bool had = ini.load();

    // Providers: one [provider:<id>] section each. Keys absent from a section
    // keep their env-seeded values (see readProvider).
    std::vector<net::ProviderSettings> list;
    std::vector<std::string> ids = ini.sectionsWith( "provider:" );
    for( std::size_t i = 0; i < ids.size(); ++i )
        list.push_back( readProvider( ini, "provider:" + ids[i], ids[i] ) );

    // Legacy single [provider] section (before multiple providers): read as
    // the provider named "default" when no new-style section exists.
    if( list.empty() && ini.has( "provider", "base_url" ) )
        list.push_back( readProvider( ini, "provider", "default" ) );

    std::string active = ini.getString( "app", "provider", std::string() );
    if( list.empty() )
        active = net::providerSettings().name;   // keep the env-seeded store
    net::replaceProviders( list, active );

    net::requestMinutes() = net::clampRequestMinutes(
        ini.getInt( "app", "request_minutes", net::requestMinutes() ) );

    // Color theme (only override when the file actually specifies one).
    std::string theme = ini.getString( "ui", "theme", std::string() );
    if( theme == "classic" )   tui::setTheme( tui::ThemeClassic );
    else if( theme == "dark" ) tui::setTheme( tui::ThemeDark );

    thinkingExpandAll() = ini.getBool( "ui", "thinking_expand",
                                       thinkingExpandAll() );
    wheelLines() = clampWheelLines( ini.getInt( "ui", "wheel_lines",
                                                wheelLines() ) );
    return had;
}

bool &thinkingExpandAll()
{
    static bool expandAll = false; // collapsed by default
    return expandAll;
}

int &wheelLines()
{
    static int lines = kDefaultWheelLines;
    return lines;
}

int clampWheelLines( int lines )
{
    if( lines < kMinWheelLines ) return kMinWheelLines;
    if( lines > kMaxWheelLines ) return kMaxWheelLines;
    return lines;
}

bool saveAll()
{
    IniFile ini( configPath() );
    ini.load();   // preserve any unrelated keys already in the file

    // Rewrite every provider from the store, and drop the sections belonging
    // to providers that no longer exist (plus the legacy [provider] section).
    ini.eraseSectionsWith( "provider:" );
    ini.eraseSection( "provider" );
    const std::vector<net::ProviderSettings> &list = net::providers();
    for( std::size_t i = 0; i < list.size(); ++i )
    {
        const net::ProviderSettings &p = list[i];
        const std::string section = "provider:" + p.name;
        ini.setString( section.c_str(), "name",  p.name );
        ini.setString( section.c_str(), "base_url", p.baseUrl );
        ini.setString( section.c_str(), "api_key",  p.apiKey );
        ini.setString( section.c_str(), "model",    p.model );
        ini.setString( section.c_str(), "models",
                       net::formatModelList( p.models ) );
        ini.setDouble( section.c_str(), "temperature", p.temperature );
        ini.setBool( section.c_str(), "thinking_resend", p.thinkingResend );
    }

    ini.setString( "app", "provider", net::providerSettings().name );
    ini.setInt( "app", "request_minutes", net::requestMinutes() );

    ini.setString( "ui", "theme", tui::themeName( tui::currentTheme() ) );
    ini.setBool( "ui", "thinking_expand", thinkingExpandAll() );
    ini.setInt( "ui", "wheel_lines", wheelLines() );

    return ini.save();
}

} // namespace atui::settings
} // namespace atui
