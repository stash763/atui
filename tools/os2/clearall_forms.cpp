/* Can clearAll() be one name with a map overload, on the 1995 library?

   core/strutil.h empties containers through clearAll(), and it needs two bodies:
   erase(begin(), end()) -- linear, and the only acceptable shape for a string or
   a vector -- and a loop of erase(begin()) for a map, because Open Watcom 1.9's
   std::map has no range erase (measured: tools/os2/maperase.cpp).  Two bodies can
   be two names or one name with an overload for std::map, and one name with an
   overload is partial ordering of function templates, which is more than a 1995
   compiler promised.  Measured here: the call to clearAll for each kind of
   container is what has to compile, not just the declarations.
*/
#include <map>
#include <string>
#include <vector>

namespace clear {

template< class C >
inline void clearAll( C &c )
{
    c.erase( c.begin(), c.end() );
}

template< class K, class V, class C, class A >
inline void clearAll( std::map< K, V, C, A > &m )
{
    while ( !m.empty() )
        m.erase( m.begin() );
}

} // namespace clear

typedef std::map< std::string, int, std::less< std::string > > IntMap;
typedef std::map< std::string, std::string, std::less< std::string > > StrMap;
typedef std::map< std::string, StrMap, std::less< std::string > > NestedMap;

void use_string( std::string &s )          { clear::clearAll( s ); }
void use_vector( std::vector<int> &v )     { clear::clearAll( v ) ; }
void use_map( IntMap &m )                  { clear::clearAll( m ); }
void use_nested( NestedMap &m )            { clear::clearAll( m ); }
void use_stringmap( StrMap &m )            { clear::clearAll( m ); }

/* and the loop body on its own, since that is what the map overload compiles to:
   erase(begin()) has to exist even where erase(first,last) and clear() do not */
void loop_only( IntMap &m )
{
    while ( !m.empty() )
        m.erase( m.begin() );
}

int main( void )
{
    return 0;
}
