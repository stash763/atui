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
// the lookup they replace rather than like an iterator loop.  The mapped type is
// one of the arguments -- see findEntry for why that is a requirement of this
// compiler rather than a style choice.

#ifndef ATUI_CORE_LOOKUP_H
#define ATUI_CORE_LOOKUP_H

namespace atui {
namespace core {

// The third argument names the mapped type: pass a null pointer of it, as in
//     const Section *s = core::findEntry( sections, name, (const Section *) 0 );
// It is not an accident and it is not unused -- it is what the compiler needs.
// Declaring this function's result `const typename M::mapped_type *`, the shape
// every reader would write, stops Borland 5.02 with
//     Fatal ...: Internal compiler error in function ...
// for *every* instantiation, including a body of `return 0;` alone, and
// including a map whose mapped type is `int`.  A pointer to
// `typename M::key_type` in the same position is fine, and so is returning the
// address as `const void *`, which pins it to the dependent return type rather
// than to the loop, the address-of, or the map itself (measured: nine variants
// of the body and of the return type spelling -- `value_type::second_type` and a
// named local included, all of them fatal).  Deducing `V` from the tag makes the
// return type non-dependent, which compiles, and keeps the type checked: a tag
// that is not the mapped type is an error at the return, not a silent cast.
template< class M, class V >
const V *findEntry( const M &m, const typename M::key_type &key, const V * )
{
    // (*it).first rather than it->first: Borland 5.02 refuses -> on this
    // library's map iterators ("Pointer to structure required on left side of ->
    // or ->*", measured on an instantiated loop) while accepting it on a
    // vector's and on a string's.  RW's map iterator is a class; the compiler
    // wants the dereference written out.
    for( typename M::const_iterator it = m.begin(); it != m.end(); ++it ) {
        if( (*it).first == key )
            return &(*it).second;
    }
    return 0;
}

// And for the cases that only wanted to know whether something is present.
//
// The walk again rather than `return 0 != findEntry( m, key );`, which is what
// this said and what a reader would expect: Borland 5.02 stops with an internal
// compiler error on a function template that calls a second function template
// whose result points into the same dependent mapped type, in the instantiation
// markdown.cpp makes -- `Fatal core/lookup.h 35: Internal compiler error in
// function atui::core::hasEntry( const std::map<std::string,
// tui::<unnamed>::Link, ...> &, ...)` -- while this body compiles.  The crash
// needed that file's context and would not reproduce in a reduced case, which is
// why the answer is a second loop rather than a claim about the compiler.  Every
// map atui looks anything up in here has a handful of entries, so nothing is
// paid for it.
template< class M >
bool hasEntry( const M &m, const typename M::key_type &key )
{
    for( typename M::const_iterator it = m.begin(); it != m.end(); ++it )
        if( (*it).first == key ) return true;
    return false;
}

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_LOOKUP_H
