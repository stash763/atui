/* What Open Watcom 1.9's std::map can be emptied with, measured rather than
   guessed.

   core/strutil.h's clearAll() empties a container with erase(begin(), end()),
   which every other target accepts, and this compiler answers E341 "number of
   arguments for function 'RedBlackTree::erase' is incorrect" for a map -- so the
   range form is not there, and a helper that serves strings, vectors and maps has
   to be written in a form all four of erase's spellings allows.

   One candidate per function, because the compiler reports each failing function
   separately: one compile says which forms exist and which do not, which is the
   difference between four round trips over the bridge and one.

   The types spell their comparator because Borland C++ 5.02 needs it spelled and
   this file has to compile on both; the second set is the map-of-maps ini.cpp
   builds, in case a value type changes the answer.
*/
#include <map>
#include <string>
#include <vector>

typedef std::map< std::string, int, std::less< std::string > > IntMap;
typedef std::map< std::string, std::string, std::less< std::string > > StrMap;
typedef std::map< std::string, StrMap, std::less< std::string > > NestedMap;

void int_range( IntMap &m )   { m.erase( m.begin(), m.end() ); }
void int_iter( IntMap &m )    { m.erase( m.begin() ); }
void int_key( IntMap &m )     { m.erase( std::string( "k" ) ); }
void int_clear( IntMap &m )   { m.clear(); }
void int_assign( IntMap &m )  { m = IntMap(); }

void nested_range( NestedMap &m )  { m.erase( m.begin(), m.end() ); }
void nested_iter( NestedMap &m )   { m.erase( m.begin() ); }
void nested_clear( NestedMap &m )  { m.clear(); }

void string_range( std::string &s )  { s.erase( s.begin(), s.end() ); }
void string_iter( std::string &s )   { s.erase( s.begin() ); }
void string_clear( std::string &s )  { s.clear(); }

void vector_range( std::vector<int> &v ) { v.erase( v.begin(), v.end() ); }
void vector_iter( std::vector<int> &v )  { v.erase( v.begin() ); }
void vector_clear( std::vector<int> &v ) { v.clear(); }

int main( void )
{
    return 0;
}
