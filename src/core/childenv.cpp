// Implementation of the child-environment helpers. See childenv.h.
#include "childenv.h"
#include "core/strutil.h"      // clearAll, eraseFrom, eraseAt: Borland has no clear()

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(__MSDOS__)
#include <process.h>
#include <stdlib.h>
// MSVC's <stdlib.h> already names the environment block, as a macro onto
// __p__environ() for the DLL runtime that every non-/MT build links.  Saying it
// again underneath that macro is how warning C4273 ("__p__environ: inconsistent
// dll linkage") appeared here on MSVC 19.16: the macro turned the declaration
// into one for the function, without its _CRTIMP linkage.  Same reasoning the
// OS/2 branch below records -- redeclare only where the header does not.
#if !defined(_MSC_VER)
// extern "C": the C library's environment block is a C object, and a bare
// `extern char **_environ;` in a C++ translation unit is a C++ one.
extern "C" { extern char **_environ; }
#endif
#define ATUI_ENV_BLOCK  _environ
// Windows and DOS copy the string handed to _putenv, so a temporary std::string is
// safe; removing a variable is the same call with an empty value.
#if defined(__BORLANDC__)
// Borland's CRT has putenv and no _putenv (measured: "Call to undefined function
// '_putenv'" with <stdlib.h> and <process.h> both included, and the same call to
// putenv compiles); what it does, and what the comment above is about, is the
// same -- it copies, and an empty value unsets.
#define ATUI_ENV_SET( n, v )  putenv( ( ( n ) + "=" + ( v ) ).c_str() )
#define ATUI_ENV_UNSET( n )   putenv( ( ( n ) + "=" ).c_str() )
#else
#define ATUI_ENV_SET( n, v )  _putenv( ( ( n ) + "=" + ( v ) ).c_str() )
#define ATUI_ENV_UNSET( n )   _putenv( ( ( n ) + "=" ).c_str() )
#endif
#elif defined(__OS2__)
#include <process.h>
#include <stdlib.h>
// OS/2 asks for the POSIX spelling of the global: Watcom's <stdlib.h> declares
// `extern char **environ` and has no _environ -- the link asked the library for
// `__environ` and was told no (measured).  It is not redeclared here on purpose:
// for a DLL runtime that name is a macro onto a function call, and both forms are
// read as an expression, which is all ATUI_ENV_BLOCK is ever used for.
//
// Otherwise OS/2 belongs with its DOS relatives rather than with POSIX: Watcom's
// C library has _putenv, which copies, and no setenv() or unsetenv() at all.
#define ATUI_ENV_BLOCK  environ
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
    clearAll( entries_ );
    clearAll( pointers_ );

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

    // And the CreateProcess block, from the same entries, in the same pass worth
    // of order.  Built here rather than on demand because the caller hands the
    // pointer straight to the API and cannot be handed a temporary.
    clearAll( block_ );
    for( std::size_t i = 0; i < entries_.size(); ++i )
    {
        block_ += entries_[i];
        block_ += '\0';
    }
    block_ += '\0';   // the empty entry that says the block has ended
}

char *const * ChildEnvironment::argv() const
{
    return pointers_.empty() ? 0 : &pointers_[0];
}

const char * ChildEnvironment::block() const
{
    return block_.c_str();
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
