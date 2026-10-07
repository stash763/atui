// Definitions for the lightweight syntax highlighter (see syntax.h).
#include "tui/syntax.h"

#include <cctype>
#include <cstring>

namespace atui {
namespace tui {

namespace {

// --- tiny helpers -----------------------------------------------------------

inline bool isSpace_( char c ) { return c == ' ' || c == '\t'; }
inline bool isDigit_( char c ) { return c >= '0' && c <= '9'; }
inline bool isIdentStart( char c )
{
    return c == '_' || std::isalpha( (unsigned char) c ) != 0;
}
inline bool isIdentChar( char c )
{
    return c == '_' || std::isalnum( (unsigned char) c ) != 0;
}
inline bool isHexDigit( char c )
{
    return isDigit_( c ) || ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' );
}

bool contains( const char *const *list, size_t n, const std::string &w )
{
    for( size_t i = 0; i < n; ++i )
        if( w == list[i] )
            return true;
    return false;
}

bool startsWith( const std::string &s, size_t i, const char *p )
{
    size_t n = std::strlen( p );
    return s.compare( i, n, p ) == 0;
}

// Find an unescaped occurrence of `ch` at or after `from`; npos if absent.
size_t findUnescaped( const std::string &s, size_t from, char ch )
{
    for( size_t i = from; i < s.size(); ++i )
    {
        if( s[i] == '\\' ) { ++i; continue; }
        if( s[i] == ch ) return i;
    }
    return std::string::npos;
}

// --- keyword / type tables --------------------------------------------------

const char *const kwC[] = {
    "auto","break","case","const","constexpr","continue","default","delete","do",
    "else","enum","explicit","export","extern","false","for","friend","goto","if",
    "inline","mutable","namespace","new","noexcept","nullptr","operator","override",
    "private","protected","public","register","return","sizeof","static",
    "static_assert","struct","switch","template","this","throw","true","typedef",
    "typename","union","using","virtual","volatile","while","final","co_await",
    "co_return","co_yield","NULL"
};
const char *const tyC[] = {
    "int","char","float","double","void","long","short","unsigned","signed","bool",
    "size_t","ssize_t","int8_t","int16_t","int32_t","int64_t","uint8_t","uint16_t",
    "uint32_t","uint64_t","wchar_t","char8_t","char16_t","char32_t","string","wstring",
    "vector","map","set","unordered_map","unordered_set","pair","array","list","FILE",
    "std"
};

const char *const kwPy[] = {
    "False","None","True","and","as","assert","async","await","break","class",
    "continue","def","del","elif","else","except","finally","for","from","global",
    "if","import","in","is","lambda","nonlocal","not","or","pass","raise","return",
    "try","while","with","yield","self","print","match","case"
};
const char *const tyPy[] = {
    "int","float","str","bool","bytes","bytearray","list","dict","set","tuple",
    "frozenset","complex","object","type","Exception","ValueError","TypeError",
    "KeyError","IndexError","RangeError","Optional","Union","List","Dict","Set",
    "Tuple","Any"
};

const char *const kwJs[] = {
    "async","await","break","case","catch","class","const","continue","debugger",
    "default","delete","do","else","enum","export","extends","false","finally",
    "for","function","if","import","in","instanceof","let","new","null","of",
    "return","static","super","switch","this","throw","true","try","typeof","var",
    "void","while","with","yield","interface","type","implements","public","private",
    "protected","readonly","namespace","declare","undefined","get","set","as","from"
};
const char *const tyJs[] = {
    "number","string","boolean","any","void","object","symbol","bigint","unknown",
    "never","Array","Promise","Map","Set","Record","Partial","Date","RegExp","Error"
};

const char *const kwJson[] = { "true","false","null" };

const char *const kwBash[] = {
    "if","then","else","elif","fi","for","while","until","do","done","case","esac",
    "function","in","select","time","return","break","continue","exit","local",
    "export","readonly","unset","declare","source","eval","shift","set","trap"
};
const char *const cmdBash[] = {
    "echo","printf","cd","ls","cp","mv","rm","mkdir","touch","cat","grep","sed","awk",
    "curl","wget","git","make","docker","npm","pip","python","bash","sh","sudo","apt",
    "brew","ssh","scp","tar","kill","ps","chmod","chown"
};

const char *const kwGo[] = {
    "break","case","chan","const","continue","default","defer","else","fallthrough",
    "for","func","go","goto","if","import","interface","map","package","range",
    "return","select","struct","switch","type","var","nil","true","false","make","new",
    "len","cap","append","panic","recover","print","println"
};
const char *const tyGo[] = {
    "bool","byte","complex64","complex128","error","float32","float64","int","int8",
    "int16","int32","int64","rune","string","uint","uint8","uint16","uint32","uint64",
    "uintptr","any"
};

const char *const kwRust[] = {
    "as","async","await","break","const","continue","crate","dyn","else","enum","extern",
    "false","fn","for","if","impl","in","let","loop","match","mod","move","mut","pub",
    "ref","return","self","Self","static","struct","super","trait","true","type","unsafe",
    "use","where","while","Box","Some","None","Ok","Err","vec","println","panic"
};
const char *const tyRust[] = {
    "i8","i16","i32","i64","i128","isize","u8","u16","u32","u64","u128","usize","f32",
    "f64","bool","char","str","String","Vec","Option","Result","HashMap","BTreeMap","Cow"
};

std::string lower( const std::string &s )
{
    std::string r = s;
    for( size_t i = 0; i < r.size(); ++i )
        r[i] = (char) std::tolower( (unsigned char) r[i] );
    return r;
}

bool isKeyword( Lang lang, const std::string &w )
{
    switch( lang )
    {
        case LangC:    return contains( kwC,   sizeof(kwC)/sizeof(*kwC),     w );
        case LangPython: return contains( kwPy, sizeof(kwPy)/sizeof(*kwPy),  w );
        case LangJS:   return contains( kwJs,  sizeof(kwJs)/sizeof(*kwJs),   w );
        case LangJSON: return contains( kwJson,sizeof(kwJson)/sizeof(*kwJson), w );
        case LangBash: return contains( kwBash,sizeof(kwBash)/sizeof(*kwBash), w )
                        || contains( cmdBash, sizeof(cmdBash)/sizeof(*cmdBash), w );
        case LangGo:   return contains( kwGo,  sizeof(kwGo)/sizeof(*kwGo),   w );
        case LangRust: return contains( kwRust,sizeof(kwRust)/sizeof(*kwRust), w );
        default:       return false;
    }
}

bool isType( Lang lang, const std::string &w )
{
    switch( lang )
    {
        case LangC:    return contains( tyC,   sizeof(tyC)/sizeof(*tyC),     w );
        case LangPython: return contains( tyPy, sizeof(tyPy)/sizeof(*tyPy),  w );
        case LangJS:   return contains( tyJs,  sizeof(tyJs)/sizeof(*tyJs),   w );
        case LangGo:   return contains( tyGo,  sizeof(tyGo)/sizeof(*tyGo),   w );
        case LangRust: return contains( tyRust,sizeof(tyRust)/sizeof(*tyRust), w );
        default:       return false;
    }
}

// Line comment introducer for a language, or NULL.
const char *lineComment( Lang lang )
{
    switch( lang )
    {
        case LangC: case LangJS: case LangGo: case LangRust: return "//";
        case LangPython: case LangBash: return "#";
        default: return 0;
    }
}

bool hasBlockComment( Lang lang )
{
    return lang == LangC || lang == LangJS || lang == LangGo || lang == LangRust;
}

} // anonymous namespace

// --- public -----------------------------------------------------------------

Lang detectLang( const std::string &info )
{
    std::string s = lower( info );
    // strip trailing attributes ("c++ comment" etc) -> first token
    size_t sp = s.find_first_of( " \t," );
    if( sp != std::string::npos ) s = s.substr( 0, sp );

    if( s == "c" || s == "h" || s == "c++" || s == "cpp" || s == "hpp" ||
        s == "cc" || s == "cxx" || s == "cuda" )
        return LangC;
    if( s == "py" || s == "python" || s == "python3" )
        return LangPython;
    if( s == "js" || s == "javascript" || s == "ts" || s == "typescript" ||
        s == "jsx" || s == "tsx" )
        return LangJS;
    if( s == "json" || s == "jsonc" || s == "json5" )
        return LangJSON;
    if( s == "sh" || s == "bash" || s == "zsh" || s == "shell" || s == "console" ||
        s == "ksh" )
        return LangBash;
    if( s == "go" || s == "golang" )
        return LangGo;
    if( s == "rs" || s == "rust" )
        return LangRust;
    return LangPlain;
}

void highlightLine( Lang lang, const std::string &code,
                    SynState &st, std::vector<Span> &out )
{
    out.clear();
    if( code.empty() )
    {
        out.push_back( Span( std::string(), rCode ) );
        return;
    }
    if( lang == LangPlain )
    {
        out.push_back( Span( code, rCode ) );
        return;
    }

    const size_t n = code.size();
    size_t i = 0;
    std::string def;
    // accumulate a default (rCode) run and flush it before a coloured span.
    struct Flush { std::vector<Span>& o; std::string& d;
        void operator()( unsigned char r ){ if( !d.empty() ){ o.push_back( Span( d, r ) ); d.clear(); } }
        void code(){ operator()( rCode ); }
    } flush = { out, def };

    // Resume an open multi-line construct.
    if( st.mode == 1 )            // block comment
    {
        size_t e = code.find( st.term, i );
        if( e == std::string::npos )
        {
            out.push_back( Span( code.substr( i ), rComment ) );
            return;
        }
        out.push_back( Span( code.substr( i, e - i + st.term.size() ), rComment ) );
        i = e + st.term.size();
        st.mode = 0; st.term.clear();
    }
    else if( st.mode == 2 )       // multi-line string
    {
        size_t e = code.find( st.term, i );
        if( e == std::string::npos )
        {
            out.push_back( Span( code.substr( i ), rString ) );
            return;
        }
        out.push_back( Span( code.substr( i, e - i + st.term.size() ), rString ) );
        i = e + st.term.size();
        st.mode = 0; st.term.clear();
    }

    // C/C++ preprocessor line (#include ...), only if '#' is the first token.
    if( lang == LangC )
    {
        size_t j = i;
        while( j < n && isSpace_( code[j] ) ) ++j;
        if( j < n && code[j] == '#' )
        {
            if( j > i ) def += code.substr( i, j - i );
            flush.code();
            size_t cs = code.find( "//", j );
            if( cs == std::string::npos )
            {
                out.push_back( Span( code.substr( j ), rPreproc ) );
                return;
            }
            out.push_back( Span( code.substr( j, cs - j ), rPreproc ) );
            i = cs;
        }
    }

    const char *lc = lineComment( lang );

    while( i < n )
    {
        char c = code[i];

        if( isSpace_( c ) )
        {
            size_t s = i;
            while( i < n && isSpace_( code[i] ) ) ++i;
            def += code.substr( s, i - s );
            continue;
        }

        // line comment
        if( lc && startsWith( code, i, lc ) )
        {
            flush.code();
            out.push_back( Span( code.substr( i ), rComment ) );
            i = n;
            break;
        }

        // block comment open
        if( hasBlockComment( lang ) && startsWith( code, i, "/*" ) )
        {
            flush.code();
            size_t e = code.find( "*/", i + 2 );
            if( e == std::string::npos )
            {
                st.mode = 1; st.term = "*/";
                out.push_back( Span( code.substr( i ), rComment ) );
                i = n;
                break;
            }
            out.push_back( Span( code.substr( i, e - i + 2 ), rComment ) );
            i = e + 2;
            continue;
        }

        // python triple-quoted string
        if( lang == LangPython &&
            ( startsWith( code, i, "\"\"\"" ) || startsWith( code, i, "'''" ) ) )
        {
            std::string d = code.substr( i, 3 );
            flush.code();
            size_t e = code.find( d, i + 3 );
            if( e == std::string::npos )
            {
                st.mode = 2; st.term = d;
                out.push_back( Span( code.substr( i ), rString ) );
                i = n;
                break;
            }
            out.push_back( Span( code.substr( i, e - i + 3 ), rString ) );
            i = e + 3;
            continue;
        }

        // JS template literal (may span lines)
        if( lang == LangJS && c == '`' )
        {
            flush.code();
            size_t e = findUnescaped( code, i + 1, '`' );
            if( e == std::string::npos )
            {
                st.mode = 2; st.term = "`";
                out.push_back( Span( code.substr( i ), rString ) );
                i = n;
                break;
            }
            out.push_back( Span( code.substr( i, e - i + 1 ), rString ) );
            i = e + 1;
            continue;
        }

        // ordinary string / char literal
        if( c == '"' || c == '\'' )
        {
            flush.code();
            size_t j = i + 1;
            bool closed = false;
            while( j < n )
            {
                if( code[j] == '\\' ) { j += 2; continue; }
                if( code[j] == c ) { closed = true; break; }
                ++j;
            }
            unsigned char role = ( c == '\'' && ( lang == LangC || lang == LangRust ) )
                                 ? rChar : rString;
            if( closed )
            {
                std::string s = code.substr( i, j - i + 1 );
                if( lang == LangJSON )
                {
                    size_t k = j + 1;
                    while( k < n && isSpace_( code[k] ) ) ++k;
                    role = ( k < n && code[k] == ':' ) ? rType : rString;
                }
                out.push_back( Span( s, role ) );
                i = j + 1;
            }
            else
            {
                out.push_back( Span( code.substr( i ), role ) );
                i = n;
                break;
            }
            continue;
        }

        // numbers
        if( isDigit_( c ) ||
            ( c == '.' && i + 1 < n && isDigit_( code[i + 1] ) ) )
        {
            flush.code();
            size_t j = i;
            if( c == '0' && i + 1 < n &&
                ( code[i+1]=='x' || code[i+1]=='X' || code[i+1]=='b' ||
                  code[i+1]=='B' || code[i+1]=='o' || code[i+1]=='O' ) )
            {
                j = i + 2;
                while( j < n && ( isHexDigit( code[j] ) || code[j] == '_' ) ) ++j;
            }
            else
            {
                while( j < n &&
                       ( isDigit_( code[j] ) || code[j]=='.' || code[j]=='_' ||
                         ( ( code[j]=='e' || code[j]=='E' ) && j + 1 < n &&
                           ( isDigit_( code[j+1] ) || code[j+1]=='+' || code[j+1]=='-' ) ) ) )
                    ++j;
            }
            out.push_back( Span( code.substr( i, j - i ), rNumber ) );
            i = j;
            continue;
        }

        // identifiers / keywords / function calls
        if( isIdentStart( c ) )
        {
            size_t j = i;
            while( j < n && isIdentChar( code[j] ) ) ++j;
            std::string w = code.substr( i, j - i );
            flush.code();
            unsigned char role = rCode;
            if( isKeyword( lang, w ) )      role = rKeyword;
            else if( isType( lang, w ) )    role = rType;
            else
            {
                size_t k = j;
                while( k < n && isSpace_( code[k] ) ) ++k;
                if( k < n && code[k] == '(' ) role = rFunction;
                else if( lang == LangBash && j == i ) role = rCode;
            }
            out.push_back( Span( w, role ) );
            i = j;
            continue;
        }

        // bash variable expansions
        if( lang == LangBash && c == '$' )
        {
            flush.code();
            size_t j = i + 1;
            if( j < n && code[j] == '{' )
            {
                size_t e = code.find( '}', j );
                size_t ee = ( e == std::string::npos ) ? n : e + 1;
                out.push_back( Span( code.substr( i, ee - i ), rType ) );
                i = ee;
                continue;
            }
            while( j < n && isIdentChar( code[j] ) ) ++j;
            if( j == i + 1 ) j = i + 2;   // "$?"-style
            out.push_back( Span( code.substr( i, j - i ), rType ) );
            i = j;
            continue;
        }

        // operators
        if( std::strchr( "+-*/%=<>!&|^~?:", c ) )
        {
            flush.code();
            out.push_back( Span( std::string( 1, c ), rOperator ) );
            ++i;
            continue;
        }

        // everything else (punctuation, brackets) rides the default run
        def += c;
        ++i;
    }

    flush.code();
}

} // namespace atui::tui
} // namespace atui
