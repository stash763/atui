// Tests for the markdown renderer and syntax highlighter.
#include "harness.h"
#include "tui/markdown.h"
#include "tui/md_style.h"
#include "tui/syntax.h"

#include <string>
#include <vector>

namespace atui_test {

namespace {

using atui::tui::Line;
using atui::tui::Span;
using atui::tui::render;

std::string textOf( const Line &l )
{
    std::string s;
    for( size_t i = 0; i < l.spans.size(); ++i )
        s += l.spans[i].text;
    return s;
}

bool hasRole( const Line &l, unsigned char r )
{
    for( size_t i = 0; i < l.spans.size(); ++i )
        if( l.spans[i].role == r )
            return true;
    return false;
}

bool hasRoleText( const std::vector<Line> &ls, unsigned char r,
                  const std::string &frag )
{
    for( size_t i = 0; i < ls.size(); ++i )
        for( size_t j = 0; j < ls[i].spans.size(); ++j )
            if( ls[i].spans[j].role == r &&
                ls[i].spans[j].text.find( frag ) != std::string::npos )
                return true;
    return false;
}

bool anyContains( const std::vector<Line> &ls, const std::string &frag )
{
    for( size_t i = 0; i < ls.size(); ++i )
        if( textOf( ls[i] ).find( frag ) != std::string::npos )
            return true;
    return false;
}

int test_md_heading()
{
    std::vector<Line> ls = render( "# Title here", 40 );
    if( ls.empty() ) return -1;
    if( textOf( ls[0] ) != "Title here" ) return -2;
    if( ls[0].spans.empty() || ls[0].spans[0].role != atui::tui::rHeading )
        return -3;
    return 0;
}

int test_md_strong_em_code()
{
    std::vector<Line> ls = render( "a **bold** *it* `cd` z", 60 );
    if( !hasRoleText( ls, atui::tui::rBold, "bold" ) ) return -1;
    if( !hasRoleText( ls, atui::tui::rItalic, "it" ) ) return -2;
    if( !hasRoleText( ls, atui::tui::rCodeInline, "cd" ) ) return -3;
    return 0;
}

int test_md_link()
{
    std::vector<Line> ls = render( "see [docs](http://x) now", 60 );
    if( !hasRoleText( ls, atui::tui::rLink, "docs" ) ) return -1;
    // the URL itself is not shown
    if( anyContains( ls, "http://x" ) ) return -2;
    return 0;
}

int test_md_bullet_list()
{
    std::vector<Line> ls = render( "- one\n- two", 40 );
    if( !anyContains( ls, "- one" ) ) return -1;
    if( !anyContains( ls, "- two" ) ) return -2;
    return 0;
}

int test_md_nested_list()
{
    std::vector<Line> ls = render( "- top\n  - child", 40 );
    bool childIndented = false;
    for( size_t i = 0; i < ls.size(); ++i )
    {
        std::string t = textOf( ls[i] );
        if( t.find( "child" ) != std::string::npos &&
            t.find( "  - child" ) == 0 )
            childIndented = true;
    }
    if( !childIndented ) return -1;
    return 0;
}

int test_md_ordered_list()
{
    std::vector<Line> ls = render( "1. first\n2. second", 40 );
    if( !anyContains( ls, "1. first" ) ) return -1;
    if( !anyContains( ls, "2. second" ) ) return -2;
    return 0;
}

int test_md_blockquote()
{
    std::vector<Line> ls = render( "> quoted", 40 );
    if( ls.empty() ) return -1;
    std::string t = textOf( ls[0] );
    if( t.find( "quoted" ) == std::string::npos ) return -2;
    if( t.compare( 0, 2, "| " ) != 0 ) return -3;
    return 0;
}

int test_md_wrapping()
{
    std::string long1 = "word ";
    for( int i = 0; i < 40; ++i ) long1 += "filler ";
    std::vector<Line> ls = render( long1, 20 );
    if( ls.size() < 3 ) return -1;             // must have wrapped
    for( size_t i = 0; i < ls.size(); ++i )
        if( (int) textOf( ls[i] ).size() > 20 ) return -2;
    return 0;
}

int test_md_table()
{
    std::vector<Line> ls = render(
        "| A | B |\n|:--|--:|\n| 1 | 2 |\n| 3 | 4 |", 40 );
    if( !anyContains( ls, "A" ) ) return -1;
    if( !anyContains( ls, "3" ) ) return -2;
    // the delimiter row must not leak as data
    if( anyContains( ls, ":--" ) ) return -3;
    return 0;
}

int test_md_code_fence_highlight()
{
    std::vector<Line> ls = render(
        "```cpp\nint x = 5; // c\n```\n", 40 );
    if( !hasRoleText( ls, atui::tui::rType, "int" ) ) return -1;
    if( !hasRoleText( ls, atui::tui::rNumber, "5" ) ) return -2;
    if( !hasRoleText( ls, atui::tui::rComment, "// c" ) ) return -3;
    return 0;
}

int test_md_code_fence_python()
{
    std::vector<Line> ls = render(
        "```python\ndef f(x):\n    return x\n```\n", 40 );
    if( !hasRoleText( ls, atui::tui::rKeyword, "def" ) ) return -1;
    if( !hasRoleText( ls, atui::tui::rKeyword, "return" ) ) return -2;
    if( !hasRoleText( ls, atui::tui::rFunction, "f" ) ) return -3;
    return 0;
}

int test_md_unclosed_fence()
{
    // A still-open fence (mid-stream) must render its body, not crash.
    std::vector<Line> ls = render( "```js\nlet a = 1;\n", 40 );
    if( !anyContains( ls, "let a = 1;" ) ) return -1;
    return 0;
}

int test_detect_lang()
{
    using atui::tui::detectLang;
    if( detectLang( "python" ) != atui::tui::LangPython ) return -1;
    if( detectLang( "py" )     != atui::tui::LangPython ) return -2;
    if( detectLang( "c++" )    != atui::tui::LangC ) return -3;
    if( detectLang( "C" )      != atui::tui::LangC ) return -4;
    if( detectLang( "ts" )     != atui::tui::LangJS ) return -5;
    if( detectLang( "json" )   != atui::tui::LangJSON ) return -6;
    if( detectLang( "sh" )     != atui::tui::LangBash ) return -7;
    if( detectLang( "golang" ) != atui::tui::LangGo ) return -8;
    if( detectLang( "rust" )   != atui::tui::LangRust ) return -9;
    if( detectLang( "weirdlang" ) != atui::tui::LangPlain ) return -10;
    if( detectLang( "cpp extra" ) != atui::tui::LangC ) return -11;
    return 0;
}

int test_syntax_block_comment_state()
{
    using atui::tui::SynState;
    using atui::tui::LangC;
    SynState st;
    std::vector<Span> a, b;
    atui::tui::highlightLine( LangC, "int a; /* begin", st, a );
    if( st.mode != 1 ) return -1;             // open block comment carried
    atui::tui::highlightLine( LangC, "still comment */ int b;", st, b );
    if( st.mode != 0 ) return -2;             // closed
    return 0;
}

int test_syntax_json_keys()
{
    using atui::tui::SynState;
    using atui::tui::LangJSON;
    SynState st;
    std::vector<Span> out;
    atui::tui::highlightLine( LangJSON, "{ \"name\": \"x\" }", st, out );
    bool key = false, str = false;
    for( size_t i = 0; i < out.size(); ++i )
    {
        if( out[i].role == atui::tui::rType  && out[i].text == "\"name\"" ) key = true;
        if( out[i].role == atui::tui::rString && out[i].text == "\"x\"" )   str = true;
    }
    if( !key || !str ) return -1;
    return 0;
}

} // anonymous namespace

void init_markdown_tests()
{
    register_test( "md: heading", &test_md_heading );
    register_test( "md: strong/em/code", &test_md_strong_em_code );
    register_test( "md: link", &test_md_link );
    register_test( "md: bullet list", &test_md_bullet_list );
    register_test( "md: nested list", &test_md_nested_list );
    register_test( "md: ordered list", &test_md_ordered_list );
    register_test( "md: blockquote", &test_md_blockquote );
    register_test( "md: wrapping", &test_md_wrapping );
    register_test( "md: table", &test_md_table );
    register_test( "md: code fence highlight", &test_md_code_fence_highlight );
    register_test( "md: code fence python", &test_md_code_fence_python );
    register_test( "md: unclosed fence", &test_md_unclosed_fence );
    register_test( "syntax: detect lang", &test_detect_lang );
    register_test( "syntax: block comment state", &test_syntax_block_comment_state );
    register_test( "syntax: json keys", &test_syntax_json_keys );
}

} // namespace atui_test
