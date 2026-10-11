// Tests for the markdown renderer and syntax highlighter.
#include "harness.h"
#include "tui/markdown.h"
#include "tui/md_style.h"
#include "tui/syntax.h"
#include "tui/thinking.h"

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

// --- thinking text: prose/code segmentation ---------------------------------

using atui::tui::splitThinking;
using atui::tui::fencedMarkdown;
using atui::tui::ThinkingSeg;

int test_thinking_prose_only()
{
    std::vector<ThinkingSeg> s = splitThinking( "just reasoning\nover two lines" );
    if( s.size() != 1 ) return -1;
    if( s[0].code ) return -2;
    if( s[0].text != "just reasoning\nover two lines" ) return -3;
    if( splitThinking( "" ).size() != 0 ) return -4;
    return 0;
}

int test_thinking_code_between_prose()
{
    std::vector<ThinkingSeg> s = splitThinking(
        "look:\n```cpp\nint x;\n```\nso yes\n" );
    if( s.size() != 3 ) return -1;
    if( s[0].code || s[0].text != "look:\n" ) return -2;
    if( !s[1].code || s[1].info != "cpp" || s[1].text != "int x;\n" ) return -3;
    if( s[2].code || s[2].text != "so yes\n" ) return -4;

    // Indented fences count, and only the first info word is the language.
    s = splitThinking( "  ```python title=x\ncode\n  ```\n" );
    if( s.size() != 1 ) return -5;
    if( !s[0].code || s[0].info != "python" || s[0].text != "code\n" ) return -6;
    return 0;
}

int test_thinking_unclosed_fence_streams_as_code()
{
    // Caught mid-block, everything after the fence is code.
    std::vector<ThinkingSeg> s = splitThinking( "typing\n```\nabc\n" );
    if( s.size() != 2 ) return -1;
    if( !s[1].code || s[1].text != "abc\n" ) return -2;

    // Tildes work too, and a body with no final newline keeps none.
    s = splitThinking( "x\n~~~sh\ncd /tmp" );
    if( s.size() != 2 ) return -3;
    if( !s[1].code || s[1].info != "sh" || s[1].text != "cd /tmp" ) return -4;
    return 0;
}

int test_thinking_fence_length_rules()
{
    // A shorter run inside a block cannot close it (CommonMark).
    std::vector<ThinkingSeg> s = splitThinking( "````\n```\nstill code\n````\nafter\n" );
    if( s.size() != 2 ) return -1;
    if( !s[0].code || s[0].text != "```\nstill code\n" ) return -2;
    if( s[1].code || s[1].text != "after\n" ) return -3;

    // A fence-looking line with a trailing word does not close a block.
    s = splitThinking( "```c\nx\n``` more\ny\n```\nz\n" );
    if( s.size() != 2 || !s[0].code ) return -4;
    if( s[0].text != "x\n``` more\ny\n" ) return -5;
    if( s[1].text != "z\n" ) return -6;
    return 0;
}

int test_thinking_fenced_markdown_renders()
{
    // The fence used for re-rendering is longer than anything inside, so the
    // code cannot close its own block.
    ThinkingSeg c;
    c.code = true; c.info = "python"; c.text = "def f():\n    return 1\n";
    std::string md = fencedMarkdown( c );
    if( md != "```python\ndef f():\n    return 1\n```\n" ) return -1;

    std::vector<Line> ls = render( md, 40 );
    if( !hasRoleText( ls, atui::tui::rKeyword, "def" ) ) return -2;
    if( !hasRoleText( ls, atui::tui::rKeyword, "return" ) ) return -3;
    for( size_t i = 0; i < ls.size(); ++i )
        if( ls[i].bg != atui::tui::rCode ) return -4;   // code background rows

    // Backticks inside the code: the fence grows past them.
    c.info = "cpp"; c.text = "auto f = `` ``` ``;\n";
    md = fencedMarkdown( c );
    if( md != "````cpp\nauto f = `` ``` ``;\n````\n" ) return -5;
    ls = render( md, 60 );
    if( !anyContains( ls, "```" ) ) return -6;

    // A body missing its final newline still closes cleanly.
    c.info = ""; c.text = "plain";
    if( fencedMarkdown( c ) != "```\nplain\n```\n" ) return -7;
    return 0;
}

int test_thinking_body_prose()
{
    // Reasoning is laid out as indented plain text with a normal background.
    std::vector<Line> ls = atui::tui::renderThinkingBody( "line one\nline two\n", 40 );
    if( ls.size() != 3 ) return -1;             // two lines + the trailing blank
    if( textOf( ls[0] ) != "   line one" ) return -2;
    if( textOf( ls[1] ) != "   line two" ) return -3;
    if( !hasRole( ls[0], atui::tui::rThinking ) ) return -4;
    if( ls[0].bg != atui::tui::rNormal ) return -5;

    // Only code blocks are interpreted: markdown syntax stays as typed.
    ls = atui::tui::renderThinkingBody( "# not a heading, **not bold**\n", 60 );
    if( ls.empty() || textOf( ls[0] ) != "   # not a heading, **not bold**" )
        return -6;
    if( hasRole( ls[0], atui::tui::rHeading ) ) return -7;
    return 0;
}

int test_thinking_body_code_block()
{
    std::vector<Line> ls = atui::tui::renderThinkingBody(
        "let me write:\n```python\ndef f(x):\n    return x\n```\nso that is it\n",
        40 );
    // Fence markers never reach the screen.
    if( anyContains( ls, "```" ) ) return -1;
    // prose, two code rows, then prose again.
    if( ls.size() != 5 ) return -2;
    if( textOf( ls[0] ) != "   let me write:" ) return -3;
    if( ls[0].bg != atui::tui::rNormal ) return -4;
    if( textOf( ls[1] ) != "   def f(x):" ) return -5;
    if( ls[1].bg != atui::tui::rCode || ls[2].bg != atui::tui::rCode ) return -6;
    if( !hasRoleText( ls, atui::tui::rKeyword, "def" ) ) return -7;
    if( !hasRoleText( ls, atui::tui::rKeyword, "return" ) ) return -8;
    if( textOf( ls[3] ) != "   so that is it" ) return -9;
    if( ls[3].bg != atui::tui::rNormal ) return -10;
    // Every row keeps the three-column reasoning indent.
    for( size_t i = 0; i < ls.size(); ++i )
        if( textOf( ls[i] ).compare( 0, 3, "   " ) != 0 ) return -11;
    return 0;
}

int test_thinking_body_unclosed_fence()
{
    // Mid-stream: the reasoning so far after an open fence is code.
    std::vector<Line> ls = atui::tui::renderThinkingBody( "think\n```\nx = 1\n", 40 );
    if( ls.size() != 2 ) return -1;
    if( ls[0].bg != atui::tui::rNormal ) return -2;
    if( ls[1].bg != atui::tui::rCode ) return -3;
    if( textOf( ls[1] ) != "   x = 1" ) return -4;
    return 0;
}

int test_md_code_bg_and_blank_lines()
{
    // Code rows must carry the code background role (so the fill spans the
    // whole row), a blank line inside a fence must be preserved as a row, and
    // prose around the fence must keep the normal background.
    std::vector<Line> ls = render(
        "intro\n\n```python\ndef f():\n\n    return 1\n```\n\nout\n", 40 );

    int codeRows = 0, blankCodeRows = 0;
    bool introOk = false, outOk = false, defIsCode = false;
    for( size_t i = 0; i < ls.size(); ++i )
    {
        std::string t = textOf( ls[i] );
        bool code = ( ls[i].bg == atui::tui::rCode );
        if( code )
        {
            ++codeRows;
            if( t.empty() ) ++blankCodeRows;
        }
        if( t == "intro" && !code ) introOk = true;
        if( t == "out"   && !code ) outOk = true;
        if( t == "def f():" )       defIsCode = code;
    }
    if( codeRows < 3 )     return -1;   // def, blank, return
    if( blankCodeRows < 1 ) return -2;  // interior blank line preserved
    if( !defIsCode )        return -3;  // code row marked as code bg
    if( !introOk )          return -4;  // prose before is not code bg
    if( !outOk )            return -5;  // prose after is not code bg
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
    register_test( "md: code bg + blank lines", &test_md_code_bg_and_blank_lines );
    register_test( "syntax: detect lang", &test_detect_lang );
    register_test( "syntax: block comment state", &test_syntax_block_comment_state );
    register_test( "syntax: json keys", &test_syntax_json_keys );
    register_test( "thinking: prose only", &test_thinking_prose_only );
    register_test( "thinking: code between prose", &test_thinking_code_between_prose );
    register_test( "thinking: unclosed fence", &test_thinking_unclosed_fence_streams_as_code );
    register_test( "thinking: fence length rules", &test_thinking_fence_length_rules );
    register_test( "thinking: fenced markdown renders", &test_thinking_fenced_markdown_renders );
    register_test( "thinking: body prose", &test_thinking_body_prose );
    register_test( "thinking: body code block", &test_thinking_body_code_block );
    register_test( "thinking: body unclosed fence", &test_thinking_body_unclosed_fence );
}

} // namespace atui_test
