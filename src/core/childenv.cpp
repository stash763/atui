// Implementation of the child-environment helpers. See childenv.h.
#include "childenv.h"

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(__MSDOS__)
#include <process.h>
#include <stdlib.h>
extern char **_environ;
#define ATUI_ENV_BLOCK  _environ
// Windows and DOS copy the string handed to _putenv, so a temporary std::string is
// safe; removing a variable is the same call with an empty value.
#define ATUI_ENV_SET( n, v )  _putenv( ( ( n ) + "=" + ( v ) ).c_str() )
#define ATUI_ENV_UNSET( n )   _putenv( ( ( n ) + "=" ).c_str() )
#else
#include <unistd.h>
extern char **environ;
#define ATUI_ENV_BLOCK  environ
// setenv() copies, which is what makes the restore in HiddenCredentials leak-free:
// putenv()/strdup() would leave a string per command for the life of the process.
#define ATUI_ENV_SET( n, v )   ::setenv( ( n ).c_str(), ( v ).c_str(), 1 )
#define ATUI_ENV_UNSET( n )    ::unsetenv( ( n ).c_str() )
#endif

namespace atui {
namespace core {

namespace {

const char *kSecretMarks[] = {
    "KEY", "TOKEN", "SECRET", "PASSWORD", "PASSWD", "CREDENTIAL",
    "PASSPHRASE", "PRIVATE"
};

std::string upperCase( const std::string &s )
{
    std::string o( s );
    for( std::size_t i = 0; i < o.size(); ++i )
        o[i] = (char) std::toupper( (unsigned char) o[i] );
    return o;
}

// "NAME=" of an environment entry, or the whole string when there is no `=`.
std::string entryName( const std::string &entry )
{
    const std::string::size_type eq = entry.find( '=' );
    return ( eq == std::string::npos ) ? entry : entry.substr( 0, eq );
}

} // anonymous namespace

bool looksSecretEnvName( const std::string &name )
{
    if( name.empty() ) return false;
    const std::string up = upperCase( name );
    for( std::size_t i = 0; i < sizeof kSecretMarks / sizeof kSecretMarks[0]; ++i )
    {
        if( up.find( kSecretMarks[i] ) != std::string::npos ) return true;
    }
    return false;
}

bool envNamePasses( const std::string &name, const std::string &passList )
{
    if( name.empty() || passList.empty() ) return false;
    const std::string want = upperCase( name );

    std::string::size_type at = 0;
    while( at <= passList.size() )
    {
        std::string::size_type end = passList.find_first_of( ",; \t", at );
        if( end == std::string::npos ) end = passList.size();
        const std::string item = upperCase( passList.substr( at, end - at ) );
        if( !item.empty() && item == want ) return true;
        if( end == passList.size() ) break;
        at = end + 1;
    }
    return false;
}

ChildEnvironment::ChildEnvironment( const std::string &passList )
{
    rebuild( passList );
}

void ChildEnvironment::rebuild( const std::string &passList )
{
    entries_.clear();
    pointers_.clear();

    for( char **e = ATUI_ENV_BLOCK; e && *e; ++e )
    {
        const std::string entry( *e );
        const std::string name = entryName( entry );
        if( looksSecretEnvName( name ) && !envNamePasses( name, passList ) )
            continue;
        entries_.push_back( entry );
    }

    // Pointers second: pushing the entries first is what keeps them pointing at
    // strings that still exist.
    pointers_.reserve( entries_.size() + 1 );
    for( std::size_t i = 0; i < entries_.size(); ++i )
        pointers_.push_back( &entries_[i][0] );
    pointers_.push_back( 0 );
}

char *const * ChildEnvironment::argv() const
{
    return pointers_.empty() ? 0 : &pointers_[0];
}

std::size_t ChildEnvironment::size() const
{
    return entries_.size();
}

HiddenCredentials::HiddenCredentials( const std::string &passList )
{
    // Names and values first, one pass over the block; the removals second.
    // unsetenv() is allowed to rebuild the environment, so an iterator into it
    // would be pointing at freed memory by the second removal.
    for( char **e = ATUI_ENV_BLOCK; e && *e; ++e )
    {
        const std::string entry( *e );
        const std::string name = entryName( entry );
        if( !looksSecretEnvName( name ) || envNamePasses( name, passList ) )
            continue;
        const std::string::size_type eq = entry.find( '=' );
        if( eq == std::string::npos ) continue;    // not a NAME=value entry
        names_.push_back( name );
        values_.push_back( entry.substr( eq + 1 ) );
    }

    for( std::size_t i = 0; i < names_.size(); ++i )
        ATUI_ENV_UNSET( names_[i] );
}

HiddenCredentials::~HiddenCredentials()
{
    // A variable that was present with an empty value comes back unset: the shell
    // cannot tell the two apart, and remembering which was which would need a
    // third list for nothing.
    for( std::size_t i = 0; i < names_.size(); ++i )
    {
        if( values_[i].empty() ) continue;
        ATUI_ENV_SET( names_[i], values_[i] );
    }
}

} // namespace atui::core
} // namespace atui
