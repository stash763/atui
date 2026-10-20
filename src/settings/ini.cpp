// Implementation of the IniFile helper. See ini.h.
#include "ini.h"

#include "core/lines.h"
#include "core/lookup.h"
#include "core/strutil.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#if defined(_WIN32)
#  include <direct.h>
#  define ATUI_MKDIR(p) _mkdir( p )
#elif defined(__OS2__)
#  include <direct.h>
// Watcom's mkdir takes no mode: <direct.h> declares `int mkdir( const char * )`.
#  define ATUI_MKDIR(p) mkdir( p )
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  define ATUI_MKDIR(p) mkdir( p, 0700 )
#endif

namespace atui {
namespace settings {

namespace {

std::string trim( const std::string &s )
{
    std::size_t b = 0, e = s.size();
    while( b < e && std::isspace( (unsigned char) s[b] ) ) ++b;
    while( e > b && std::isspace( (unsigned char) s[e - 1] ) ) --e;
    return s.substr( b, e - b );
}

// ASCII lowercase; keys/sections are case-insensitive.
std::string lower( const std::string &s )
{
    std::string o( s );
    for( std::size_t i = 0; i < o.size(); ++i )
        o[i] = (char) std::tolower( (unsigned char) o[i] );
    return o;
}

// Create every directory component of 'path' (not the file itself).
// Existing directories are skipped (mkdir failures are ignored).
void ensureParentDir( const std::string &path )
{
    std::size_t slash = path.find_last_of( "/\\" );
    if( slash == std::string::npos ) return;   // no directory part (cwd)
    std::string dir = path.substr( 0, slash );
    if( dir.empty() ) return;

    std::string acc;
    for( std::size_t i = 0; i <= dir.size(); ++i )
    {
        bool sep = ( i == dir.size() || dir[i] == '/' || dir[i] == '\\' );
        if( !sep )
        {
            acc += dir[i];
            continue;
        }
        if( !acc.empty() )
            ATUI_MKDIR( acc.c_str() );          // ignore "already exists"
        if( i != dir.size() )
            acc = acc.empty() ? std::string( "/" ) : acc + "/";
    }
}

} // anonymous namespace

IniFile::IniFile( const std::string &path )
    : path_( path ), loaded_( false ) {}

void IniFile::setPath( const std::string &path ) { path_ = path; }
const std::string &IniFile::path() const { return path_; }
bool IniFile::loaded() const { return loaded_; }

bool IniFile::load()
{
    loaded_ = false;
    if( path_.empty() ) return false;

    std::ifstream in( path_.c_str(), std::ios::in );
    if( !in ) return false;

    data_.clear();
    std::string line, section = "general";
    while( core::readLine( in, line ) )
    {
        std::string s = trim( line );
        if( s.empty() ) continue;
        if( s[0] == '#' || s[0] == ';' ) continue;

        if( s[0] == '[' )
        {
            std::size_t close = s.find( ']' );
            if( close != std::string::npos )
                section = lower( trim( s.substr( 1, close - 1 ) ) );
            continue;
        }

        std::size_t eq = s.find( '=' );
        if( eq == std::string::npos ) continue;
        std::string key = lower( trim( s.substr( 0, eq ) ) );
        if( key.empty() ) continue;
        std::string value = trim( s.substr( eq + 1 ) );
        data_[section][key] = value;
    }

    loaded_ = true;
    return true;
}

bool IniFile::save() const
{
    if( path_.empty() ) return false;
    ensureParentDir( path_ );

    std::ofstream out( path_.c_str(), std::ios::out | std::ios::trunc );
    if( !out ) return false;

    for( std::map<std::string, Section>::const_iterator si = data_.begin();
         si != data_.end(); ++si )
    {
        // `.c_str()` on every std::string: Watcom's streams have no operator<<
        // for std::string at all (measured: "left expression must be integral"),
        // and a char pointer works on every compiler atui builds with.
        out << "[" << si->first.c_str() << "]\n";
        for( Section::const_iterator ki = si->second.begin();
             ki != si->second.end(); ++ki )
        {
            out << ki->first.c_str() << " = " << ki->second.c_str() << "\n";
        }
        out << "\n";
    }
    out.flush();
    if( !out.good() ) return false;
    out.close();

#if !defined(_WIN32) && !defined(__OS2__) && !defined(__DOS__)
    // Restrictive perms: the file may contain secrets (api_key).  OS/2 and DOS
    // are skipped: their C library chmod knows only the read-only bit, which is
    // not what this is asking for.
    chmod( path_.c_str(), 0600 );
#endif
    return true;
}

bool IniFile::has( const char *section, const char *key ) const
{
    const Section *s = core::findEntry( data_, lower( section ? section : "" ) );
    if( 0 == s ) return false;
    return core::hasEntry( *s, lower( key ? key : "" ) );
}

std::vector<std::string> IniFile::sectionsWith( const std::string &prefix ) const
{
    std::vector<std::string> out;
    const std::string p = lower( prefix );
    for( std::map<std::string, Section>::const_iterator it = data_.begin();
         it != data_.end(); ++it )
    {
        if( core::startsWith( it->first, p.c_str() ) && it->first.size() > p.size() )
            out.push_back( it->first.substr( p.size() ) );
    }
    return out;
}

std::size_t IniFile::eraseSection( const std::string &name )
{
    return data_.erase( lower( name ) );
}

std::size_t IniFile::eraseSectionsWith( const std::string &prefix )
{
    const std::string p = lower( prefix );
    std::size_t n = 0;
    for( std::map<std::string, Section>::iterator it = data_.begin();
         it != data_.end(); )
    {
        if( core::startsWith( it->first, p.c_str() ) )
        {
            std::map<std::string, Section>::iterator gone = it++;
            data_.erase( gone );
            ++n;
        }
        else ++it;
    }
    return n;
}

std::string IniFile::getString( const char *section, const char *key,
                                const std::string &def ) const
{
    const Section *s = core::findEntry( data_, lower( section ? section : "" ) );
    if( 0 == s ) return def;
    const std::string *v = core::findEntry( *s, lower( key ? key : "" ) );
    if( 0 == v ) return def;
    return *v;
}

int IniFile::getInt( const char *section, const char *key, int def ) const
{
    std::string v = getString( section, key, std::string() );
    if( v.empty() ) return def;
    char *end = 0;
    long l = std::strtol( v.c_str(), &end, 10 );
    if( end == v.c_str() ) return def;
    return (int) l;
}

double IniFile::getDouble( const char *section, const char *key, double def ) const
{
    std::string v = getString( section, key, std::string() );
    if( v.empty() ) return def;
    char *end = 0;
    double d = std::strtod( v.c_str(), &end );
    if( end == v.c_str() ) return def;
    return d;
}

bool IniFile::getBool( const char *section, const char *key, bool def ) const
{
    std::string v = lower( getString( section, key, std::string() ) );
    if( v.empty() ) return def;
    if( v == "1" || v == "true" || v == "yes" || v == "on" ) return true;
    if( v == "0" || v == "false" || v == "no" || v == "off" ) return false;
    return def;
}

void IniFile::setString( const char *section, const char *key,
                         const std::string &value )
{
    data_[lower( section ? section : "" )][lower( key ? key : "" )] = value;
}

long IniFile::getLong( const char *section, const char *key, long def ) const
{
    std::string v = getString( section, key, std::string() );
    if( v.empty() ) return def;
    char *end = 0;
    const long l = std::strtol( v.c_str(), &end, 10 );
    if( end == v.c_str() ) return def;
    return l;
}

void IniFile::setLong( const char *section, const char *key, long value )
{
    core::Fmt os;
    os << value;
    setString( section, key, os.str() );
}

void IniFile::setInt( const char *section, const char *key, int value )
{
    core::Fmt os;
    os << value;
    setString( section, key, os.str() );
}

void IniFile::setDouble( const char *section, const char *key, double value )
{
    core::Fmt os;
    os.precision( 6 );
    os << value;
    setString( section, key, os.str() );
}

void IniFile::setBool( const char *section, const char *key, bool value )
{
    setString( section, key, value ? "true" : "false" );
}

} // namespace atui::settings
} // namespace atui
