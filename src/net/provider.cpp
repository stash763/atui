// Implementation of the provider settings singleton.
// No libcurl dependency: safe to compile in non-network builds.
#include "provider.h"

#include <cstdlib>

namespace atui {
namespace net {

namespace {

const char *envOr( const char *name, const char *fallback )
{
    const char *v = getenv( name );
    return ( v && v[0] ) ? v : fallback;
}

ProviderSettings defaults()
{
    ProviderSettings s;
    s.baseUrl = envOr( "ATUI_BASE_URL", "http://127.0.0.1:8080/v1" );
    s.apiKey = envOr( "ATUI_API_KEY", "" );
    s.model = envOr( "ATUI_MODEL", "llama" );
    const char *t = getenv( "ATUI_TEMPERATURE" );
    s.temperature = ( t && t[0] ) ? std::atof( t ) : 0.7;
    const char *tr = getenv( "ATUI_THINKING_RESEND" );
    s.thinkingResend = ( tr && ( tr[0] == '1' || tr[0] == 'y' ||
                                 tr[0] == 'Y' || tr[0] == 't' || tr[0] == 'T' ) );
    return s;
}

} // anonymous namespace

ProviderSettings& providerSettings()
{
    static ProviderSettings settings = defaults();
    return settings;
}

} // namespace atui::net
} // namespace atui