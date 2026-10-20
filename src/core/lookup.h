// Looking things up in a map that is const.
//
// Watcom's map cannot be searched through a const map: `find()` on one is
// refused ("cannot call non-const function for a constant object") and there is
// no `count()` at all ("member 'count' has not been declared").  Both were
// measured on the box.  atui needs the answer -- ini sections, the theme's
// colour table, markdown's link references, the settings cache -- and those maps
// have a handful of entries in them, so a walk is not a performance decision
// here, it is what the library will do.
//
// Returns 0 when the key is not there, which keeps the call sites shaped like
// the lookup they replace rather than like an iterator loop.

#ifndef ATUI_CORE_LOOKUP_H
#define ATUI_CORE_LOOKUP_H

namespace atui {
namespace core {

template< class M >
const typename M::mapped_type *findEntry( const M &m,
                                          const typename M::key_type &key )
{
    for( typename M::const_iterator it = m.begin(); it != m.end(); ++it ) {
        if( it->first == key )
            return &(it->second);
    }
    return 0;
}

// And for the cases that only wanted to know whether something was present.
template< class M >
bool hasEntry( const M &m, const typename M::key_type &key )
{
    return 0 != findEntry( m, key );
}

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_LOOKUP_H
