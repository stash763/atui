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

    // Provider endpoint settings. Keys absent from the file keep their
    // current (env-seeded) values, since getString/getDouble return the
    // default we pass through.
    net::ProviderSettings &p = net::providerSettings();
    p.baseUrl     = ini.getString( "provider", "base_url",    p.baseUrl );
    p.apiKey      = ini.getString( "provider", "api_key",     p.apiKey );
    p.model       = ini.getString( "provider", "model",       p.model );
    p.temperature = ini.getDouble( "provider", "temperature", p.temperature );

    // Color theme (only override when the file actually specifies one).
    std::string theme = ini.getString( "ui", "theme", std::string() );
    if( theme == "classic" )   tui::setTheme( tui::ThemeClassic );
    else if( theme == "dark" ) tui::setTheme( tui::ThemeDark );

    return had;
}

bool saveAll()
{
    IniFile ini( configPath() );
    ini.load();   // preserve any unrelated keys already in the file

    net::ProviderSettings &p = net::providerSettings();
    ini.setString( "provider", "base_url",    p.baseUrl );
    ini.setString( "provider", "api_key",     p.apiKey );
    ini.setString( "provider", "model",       p.model );
    ini.setDouble( "provider", "temperature", p.temperature );

    ini.setString( "ui", "theme", tui::themeName( tui::currentTheme() ) );

    return ini.save();
}

} // namespace atui::settings
} // namespace atui
