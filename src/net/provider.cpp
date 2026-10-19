// Implementation of the multi-provider settings store. See provider.h.
// No libcurl dependency: safe to compile in non-network builds.
#include "provider.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace atui {
namespace net {

namespace {

const char *envOr( const char *name, const char *fallback )
{
    const char *v = std::getenv( name );
    return ( v && v[0] ) ? v : fallback;
}

bool truthy( const char *v )
{
    return v && ( v[0] == '1' || v[0] == 'y' ||
                  v[0] == 'Y' || v[0] == 't' || v[0] == 'T' );
}

std::string trim( const std::string &s )
{
    std::size_t b = 0, e = s.size();
    while( b < e && std::isspace( (unsigned char) s[b] ) ) ++b;
    while( e > b && std::isspace( (unsigned char) s[e - 1] ) ) --e;
    return s.substr( b, e - b );
}

std::string lower( const std::string &s )
{
    std::string o( s );
    for( std::size_t i = 0; i < o.size(); ++i )
        o[i] = (char) std::tolower( (unsigned char) o[i] );
    return o;
}

bool sameName( const std::string &a, const std::string &b )
{
    return lower( a ) == lower( b );
}

// Process-wide store. Seeded with one provider built from the environment.
struct Store {
    std::vector<ProviderSettings> list;
    std::size_t active;

    Store() : active( 0 )
    {
        list.push_back( defaultProviderSettings() );
    }
};

Store& store()
{
    static Store s;
    return s;
}

} // anonymous namespace

ProviderSettings defaultProviderSettings()
{
    ProviderSettings s;
    s.name = "default";
    s.baseUrl = envOr( "ATUI_BASE_URL", "http://127.0.0.1:8080/v1" );
    s.apiKey = envOr( "ATUI_API_KEY", "" );
    s.model = envOr( "ATUI_MODEL", "llama" );
    const char *t = std::getenv( "ATUI_TEMPERATURE" );
    s.temperature = ( t && t[0] ) ? std::atof( t ) : 0.7;
    s.thinkingResend = truthy( std::getenv( "ATUI_THINKING_RESEND" ) );
    s.models = parseModelList( envOr( "ATUI_MODELS", "" ) );
    if( s.models.empty() ) s.models.push_back( s.model );
    return s;
}

std::vector<ProviderSettings>& providers()
{
    return store().list;
}

std::size_t& activeProviderIndex()
{
    Store &s = store();
    if( s.active >= s.list.size() ) s.active = 0;
    return s.active;
}

ProviderSettings& providerSettings()
{
    Store &s = store();
    if( s.list.empty() ) s.list.push_back( defaultProviderSettings() );
    if( s.active >= s.list.size() ) s.active = 0;
    return s.list[ s.active ];
}

void setActiveProvider( std::size_t index )
{
    Store &s = store();
    if( s.list.empty() ) s.list.push_back( defaultProviderSettings() );
    s.active = ( index < s.list.size() ) ? index : 0;
}

bool setActiveProviderByName( const std::string &name )
{
    std::vector<ProviderSettings> &v = providers();
    for( std::size_t i = 0; i < v.size(); ++i )
    {
        if( sameName( v[i].name, name ) )
        {
            setActiveProvider( i );
            return true;
        }
    }
    return false;
}

std::string uniqueProviderName( const std::string &base )
{
    std::string want = trim( base );
    if( want.empty() ) want = "provider";
    const std::string stem = want;
    std::vector<ProviderSettings> &v = providers();
    for( int n = 2; ; ++n )
    {
        bool taken = false;
        for( std::size_t i = 0; i < v.size(); ++i )
        {
            if( sameName( v[i].name, want ) ) { taken = true; break; }
        }
        if( !taken ) return want;
        std::ostringstream suffix;
        suffix << '-' << n;
        want = stem + suffix.str();
    }
}

std::size_t addProvider( const std::string &name )
{
    Store &s = store();
    ProviderSettings p = defaultProviderSettings();
    p.name = uniqueProviderName( name );
    p.models.clear();
    if( !p.model.empty() ) p.models.push_back( p.model );
    s.list.push_back( p );
    return s.list.size() - 1;
}

bool removeProvider( std::size_t index )
{
    Store &s = store();
    if( index >= s.list.size() || s.list.size() <= 1 ) return false;
    s.list.erase( s.list.begin() + index );
    if( s.active >= s.list.size() ) s.active = 0;
    return true;
}

ProviderSettings *findProvider( const std::string &name )
{
    std::vector<ProviderSettings> &v = providers();
    for( std::size_t i = 0; i < v.size(); ++i )
        if( sameName( v[i].name, name ) )
            return &v[i];
    return 0;
}

void setModels( ProviderSettings &provider,
                const std::vector<std::string> &models )
{
    provider.models = models;
    if( provider.model.empty() )
    {
        if( !provider.models.empty() ) provider.model = provider.models.front();
        return;
    }
    // Keep the selected model even when the endpoint does not report it:
    // silently switching models on a refresh would be worse than a request that
    // fails with a clear "model not found" from the server.
    if( std::find( provider.models.begin(), provider.models.end(),
                   provider.model ) == provider.models.end() )
    {
        provider.models.insert( provider.models.begin(), provider.model );
    }
}

void selectModel( ProviderSettings &provider, const std::string &model )
{
    if( model.empty() ) return;
    if( std::find( provider.models.begin(), provider.models.end(), model ) ==
        provider.models.end() )
    {
        provider.models.push_back( model );
    }
    provider.model = model;
}

std::vector<std::string> parseModelList( const std::string &text )
{
    std::vector<std::string> out;
    std::size_t i = 0;
    while( i <= text.size() )
    {
        std::size_t comma = text.find( ',', i );
        std::string item = trim( text.substr(
            i, comma == std::string::npos ? std::string::npos : comma - i ) );
        if( !item.empty() ) out.push_back( item );
        if( comma == std::string::npos ) break;
        i = comma + 1;
    }
    return out;
}

std::string formatModelList( const std::vector<std::string> &models )
{
    std::string out;
    for( std::size_t i = 0; i < models.size(); ++i )
    {
        if( i ) out += ',';
        out += models[i];
    }
    return out;
}

void replaceProviders( const std::vector<ProviderSettings> &list,
                       const std::string &activeName )
{
    if( list.empty() ) return;
    Store &s = store();
    s.list = list;
    s.active = 0;
    for( std::size_t i = 0; i < s.list.size(); ++i )
    {
        if( sameName( s.list[i].name, activeName ) )
        {
            s.active = i;
            break;
        }
    }
}

namespace {

// A whole-number environment variable, or `fallback` when it is unset, empty
// or not a number.
int envMinutes( const char *name, int fallback )
{
    const char *v = std::getenv( name );
    if( !v || !v[0] ) return fallback;
    char *end = 0;
    const long n = std::strtol( v, &end, 10 );
    if( end == v ) return fallback;
    return (int) n;
}

} // anonymous namespace

int &requestMinutes()
{
    static int minutes = clampRequestMinutes(
        envMinutes( "ATUI_REQUEST_MINUTES", kDefaultRequestMinutes ) );
    return minutes;
}

int clampRequestMinutes( int minutes )
{
    if( minutes < kMinRequestMinutes ) return kMinRequestMinutes;
    if( minutes > kMaxRequestMinutes ) return kMaxRequestMinutes;
    return minutes;
}

} // namespace net
} // namespace atui
