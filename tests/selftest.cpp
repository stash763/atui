#include "harness.h"
#include "core/string.h"
#include <cstring>

namespace atui_test {

namespace {

int test_string_empty()
{
    atui::core::String s;
    if( s.size() != 0 )
        return -1;
    if( s.c_str() == 0 || std::strcmp( s.c_str(), "" ) != 0 )
        return -1;
    if( s.compareTo( atui::core::String() ) != 0 )
        return -1;
    if( !s.equals( "" ) )
        return -1;
    return 0;
}

int test_string_append()
{
    atui::core::String s;
    s.append( "atui" );
    s.append( " " );
    s.append( "selftest" );
    if( s.size() != 13 )
        return -1;
    if( std::strcmp( s.c_str(), "atui selftest" ) != 0 )
        return -1;
    return 0;
}

int test_string_operator_plus_equals()
{
    atui::core::String s( "hello" );
    s += " world";
    if( std::strcmp( s.c_str(), "hello world" ) != 0 )
        return -1;
    atui::core::String a;
    atui::core::String b( "x" );
    a += b;
    if( a.size() != 1 || std::strcmp( a.c_str(), "x" ) != 0 )
        return -1;
    return 0;
}

int test_string_copy_assign()
{
    atui::core::String s( "copy me" );
    atui::core::String t( s );
    if( t.size() != 7 )
        return -1;
    if( std::strcmp( t.c_str(), "copy me" ) != 0 )
        return -1;

    atui::core::String u;
    u = s;
    if( u.size() != 7 )
        return -1;
    if( std::strcmp( u.c_str(), "copy me" ) != 0 )
        return -1;

    // independent storage after modifying t
    atui::core::String v = s;
    v.clear();
    if( s.size() != 7 || std::strcmp( s.c_str(), "copy me" ) != 0 )
        return -1;
    return 0;
}

int test_string_truncate()
{
    atui::core::String s( "0123456789" );
    atui::core::String sub = s.substr( 2 );
    if( sub.size() != 8 || std::strcmp( sub.c_str(), "23456789" ) != 0 )
        return -1;

    atui::core::String sub2 = s.substr( 2, 5 );
    if( sub2.size() != 3 || std::strcmp( sub2.c_str(), "234" ) != 0 )
        return -1;

    atui::core::String sub3 = s.substr( 0, 0 );
    if( sub3.size() != 0 )
        return -1;

    atui::core::String sub4 = s.substr( 15 );
    if( sub4.size() != 0 )
        return -1;
    return 0;
}

int test_string_indexOf()
{
    atui::core::String s( "hello world" );
    if( s.indexOf( "world" ) != 6 )
        return -1;
    if( s.indexOf( "hello" ) != 0 )
        return -1;
    if( s.indexOf( "world", 1 ) != 6 )
        return -1;
    if( s.indexOf( "world", 10 ) != -1 )
        return -1;
    if( s.indexOf( "xyz" ) != -1 )
        return -1;
    if( s.indexOf( "" ) != 0 )
        return -1;
    return 0;
}

int test_string_compareTo()
{
    atui::core::String a( "abc" );
    atui::core::String b( "abc" );
    atui::core::String c( "abd" );
    atui::core::String d( "ab" );
    if( a.compareTo( b ) != 0 )
        return -1;
    if( a.compareTo( c ) > 0 )
        return -1;
    // "abc" > "ab" (d is a prefix of a)
    if( a.compareTo( d ) < 0 )
        return -1;
    if( d.compareTo( c ) > 0 )
        return -1;
    return 0;
}

} // namespace

void init()
{
    register_test( "string: empty", &test_string_empty );
    register_test( "string: append", &test_string_append );
    register_test( "string: +=", &test_string_operator_plus_equals );
    register_test( "string: copy/assign", &test_string_copy_assign );
    register_test( "string: substr", &test_string_truncate );
    register_test( "string: indexOf", &test_string_indexOf );
    register_test( "string: compareTo", &test_string_compareTo );
}

} // namespace atui_test
