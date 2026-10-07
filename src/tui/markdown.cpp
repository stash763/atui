// Definitions for the markdown renderer (see markdown.h).
#include "tui/markdown.h"
#include "tui/syntax.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace atui {
namespace tui {

namespace {

const char kHard = '\x01';   // sentinel: forced line break inside inline text

// --- char / string helpers --------------------------------------------------

inline bool isSp( char c ) { return c == ' ' || c == '\t'; }

int leadSpaces( const std::string &s )
{
    int n = 0;
    while( n < (int) s.size() && s[n] == ' ' ) ++n;
    return n;
}

std::string ltrim( const std::string &s )
{
    size_t a = 0;
    while( a < s.size() && isSp( s[a] ) ) ++a;
    return s.substr( a );
}

std::string rtrim( const std::string &s )
{
    size_t b = s.size();
    while( b > 0 && isSp( s[b - 1] ) ) --b;
    return s.substr( 0, b );
}

std::string trim( const std::string &s ) { return rtrim( ltrim( s ) ); }

std::string lower( const std::string &s )
{
    std::string r = s;
    for( size_t i = 0; i < r.size(); ++i )
        r[i] = (char) std::tolower( (unsigned char) r[i] );
    return r;
}

// --- link reference definitions --------------------------------------------

struct Link { std::string url, title; };
typedef std::map<std::string, Link> LinkMap;

// --- inline span emission ---------------------------------------------------

unsigned char combineRole( bool heading, bool bold, bool italic, bool strike )
{
    if( strike )        return rStrike;
    if( bold && italic) return rBoldItalic;
    if( bold )          return rBold;
    if( italic )        return rItalic;
    return heading ? rHeading : rNormal;
}

int runLen( const std::string &s, size_t i, size_t to, char d )
{
    int n = 0;
    while( i + n < to && s[i + n] == d ) ++n;
    return n;
}

// Find an unescaped char `d` in [from,to).
size_t findChar( const std::string &s, size_t from, size_t to, char d )
{
    for( size_t i = from; i < to; ++i )
    {
        if( s[i] == '\\' ) { ++i; continue; }
        if( s[i] == d ) return i;
    }
    return std::string::npos;
}

size_t findCloseBracket( const std::string &s, size_t from, size_t to )
{
    int depth = 0;
    for( size_t i = from; i < to; ++i )
    {
        if( s[i] == '\\' ) { ++i; continue; }
        if( s[i] == '[' ) ++depth;
        else if( s[i] == ']' )
        {
            if( depth == 0 ) return i;
            --depth;
        }
    }
    return std::string::npos;
}

// A closer for an emphasis opener of delimiter `d` with the run ending at
// `after` (first char past the opener). Requires a non-space just before p.
size_t findEmphCloser( const std::string &s, size_t after, size_t to,
                       char d, int need )
{
    for( size_t i = after; i < to; )
    {
        if( s[i] == '\\' ) { i += 2; continue; }
        if( s[i] == d )
        {
            int r = runLen( s, i, to, d );
            if( r >= need )
                return i;
            i += r;
            continue;
        }
        ++i;
    }
    return std::string::npos;
}

// Parse inline markdown, appending styled spans to `out`.
void inlineRuns( const std::string &s, size_t from, size_t to,
                 bool heading, bool bold, bool italic, bool strike,
                 const LinkMap &links, std::vector<Span> &out )
{
    std::string buf;
    const unsigned char baseRole = combineRole( heading, bold, italic, strike );
    auto flush = [&]( void )
    {
        if( !buf.empty() ) { out.push_back( Span( buf, baseRole ) ); buf.clear(); }
    };

    for( size_t i = from; i < to; )
    {
        char c = s[i];

        // backslash escape
        if( c == '\\' && i + 1 < to )
        {
            char nx = s[i + 1];
            if( std::strchr( "\\`*_{}[]()#+-.!~<>\"'", nx ) )
            { buf += nx; i += 2; continue; }
        }

        // inline code span (run of backticks)
        if( c == '`' )
        {
            int n1 = runLen( s, i, to, '`' );
            size_t j = i + n1;
            // find a run of exactly n1 backticks
            size_t p = j;
            bool found = false;
            while( p < to )
            {
                if( s[p] == '`' )
                {
                    int r = runLen( s, p, to, '`' );
                    if( r == n1 ) { found = true; break; }
                    p += r;
                    continue;
                }
                ++p;
            }
            if( found )
            {
                std::string code = s.substr( j, p - j );
                // strip one leading/trailing space (CommonMark)
                if( code.size() >= 2 && code.front() == ' ' &&
                    code.back() == ' ' )
                    code = code.substr( 1, code.size() - 2 );
                flush();
                out.push_back( Span( code, rCodeInline ) );
                i = p + n1;
                continue;
            }
        }

        // image ![alt](url)
        if( c == '!' && i + 1 < to && s[i + 1] == '[' )
        {
            size_t rb = findCloseBracket( s, i + 2, to );
            if( rb != std::string::npos && rb + 1 < to && s[rb + 1] == '(' )
            {
                size_t rp = findChar( s, rb + 2, to, ')' );
                if( rp != std::string::npos )
                {
                    flush();
                    std::string alt = s.substr( i + 2, rb - ( i + 2 ) );
                    out.push_back( Span( alt.empty() ? "image" : alt, rLink ) );
                    i = rp + 1;
                    continue;
                }
            }
            buf += c; ++i; continue;
        }

        // autolink <scheme:...>
        if( c == '<' )
        {
            size_t gt = findChar( s, i + 1, to, '>' );
            std::string inner = ( gt == std::string::npos )
                ? std::string() : s.substr( i + 1, gt - i - 1 );
            if( gt != std::string::npos &&
                ( inner.compare( 0, 7, "http://" )  == 0 ||
                  inner.compare( 0, 8, "https://" ) == 0 ||
                  inner.compare( 0, 7, "mailto:" )  == 0 ) )
            {
                flush();
                out.push_back( Span( inner, rLink ) );
                i = gt + 1;
                continue;
            }
            buf += c; ++i; continue;
        }

        // footnote marker [^id]
        if( c == '[' && i + 1 < to && s[i + 1] == '^' )
        {
            size_t rb = findChar( s, i + 2, to, ']' );
            if( rb != std::string::npos )
            {
                flush();
                out.push_back( Span( s.substr( i, rb - i + 1 ), rLink ) );
                i = rb + 1;
                continue;
            }
        }

        // link [text](url "t") / [text][ref] / [ref]
        if( c == '[' )
        {
            size_t rb = findCloseBracket( s, i + 1, to );
            if( rb != std::string::npos )
            {
                std::string label = s.substr( i + 1, rb - i - 1 );
                // inline [text](url)
                if( rb + 1 < to && s[rb + 1] == '(' )
                {
                    size_t rp = findChar( s, rb + 2, to, ')' );
                    if( rp != std::string::npos )
                    {
                        flush();
                        out.push_back( Span( label.empty() ? "(link)" : label,
                                             rLink ) );
                        i = rp + 1;
                        continue;
                    }
                }
                // reference [text][ref] or [ref][]
                if( rb + 1 < to && s[rb + 1] == '[' )
                {
                    size_t rb2 = findChar( s, rb + 2, to, ']' );
                    if( rb2 != std::string::npos )
                    {
                        std::string ref = s.substr( rb + 2, rb2 - rb - 2 );
                        if( ref.empty() ) ref = label;
                        LinkMap::const_iterator it = links.find( lower( ref ) );
                        if( it != links.end() )
                        {
                            flush();
                            out.push_back( Span( label, rLink ) );
                            i = rb2 + 1;
                            continue;
                        }
                    }
                }
                // collapsed [ref]
                LinkMap::const_iterator it = links.find( lower( label ) );
                if( it != links.end() )
                {
                    flush();
                    out.push_back( Span( label, rLink ) );
                    i = rb + 1;
                    continue;
                }
            }
            buf += c; ++i; continue;
        }

        // strikethrough ~~text~~
        if( c == '~' )
        {
            int r = runLen( s, i, to, '~' );
            if( r >= 2 )
            {
                size_t p = findEmphCloser( s, i + r, to, '~', 2 );
                if( p != std::string::npos )
                {
                    flush();
                    inlineRuns( s, i + r, p, heading, bold, italic, true,
                                links, out );
                    i = p + 2;
                    continue;
                }
            }
            buf += c; ++i; continue;
        }

        // emphasis / strong for '*' and '_'
        if( c == '*' || c == '_' )
        {
            bool underscore = ( c == '_' );
            int r = runLen( s, i, to, c );
            size_t after = i + r;
            bool openOk = after < to && !isSp( s[after] ) && s[after] != c;
            if( underscore && r < 3 )
            {
                // require a word boundary before '_' (no snake_case emphasis)
                bool prevWord = ( i > 0 ) &&
                    ( std::isalnum( (unsigned char) s[i - 1] ) || s[i - 1] == '_' );
                if( prevWord ) { buf += c; i += r; continue; }
            }
            if( openOk && r >= 1 )
            {
                // pick strong+emph / strong / em
                int style = ( r >= 3 ) ? 3 : ( r == 2 ? 2 : 1 );
                int need = style;
                size_t p = findEmphCloser( s, after, to, c, need );
                if( p != std::string::npos && p > after && !isSp( s[p - 1] ) )
                {
                    flush();
                    bool nb = bold, ni = italic;
                    if( style >= 2 ) nb = true;
                    if( style == 1 || style == 3 ) ni = true;
                    inlineRuns( s, after, p, heading, nb, ni, strike, links, out );
                    i = p + need;
                    continue;
                }
            }
            buf += std::string( r, c ); i = after; continue;
        }

        buf += c; ++i;
    }
    flush();
}

// unescape a plain string (for table cells we render raw)

// --- layout -----------------------------------------------------------------

struct Ctx
{
    int width;
    LinkMap links;
};

// Places content (word-wrapped) into `out`, one Line per row, prefixing every
// row with `fp` (first row) / `cp` (continuation rows).
class Layout
{
public:
    Layout( const Ctx &ctx, const std::string &fp, const std::string &cp,
            unsigned char fpRole, unsigned char cpRole, std::vector<Line> &out )
        : ctx_( ctx ), fp_( fp ), cp_( cp ), fpRole_( fpRole ), cpRole_( cpRole ),
          out_( out ), cw_( 0 ), first_( true )
    {
        startLine();
    }

    void addWord( const std::string &t, unsigned char r )
    {
        if( t.empty() ) return;
        if( (int) t.size() > avail_ )
        {
            splitLong( t, r );
            return;
        }
        int add = cw_ > 0 ? 1 : 0;
        if( cw_ + add + (int) t.size() > avail_ )
        {
            endLine();
            add = 0;
        }
        if( add ) { line_.push_back( Span( " ", rNormal ) ); cw_ += 1; }
        line_.push_back( Span( t, r ) );
        cw_ += (int) t.size();
    }

    void addRaw( const std::string &t, unsigned char r )   // no word split
    {
        size_t pos = 0;
        while( pos < t.size() )
        {
            int room = avail_ - cw_;
            if( room <= 0 ) { endLine(); room = avail_; }
            int take = (int) std::min( (size_t) room, t.size() - pos );
            line_.push_back( Span( t.substr( pos, take ), r ) );
            cw_ += take; pos += take;
        }
    }

    void hardBreak() { endLine(); }

    // feed inline spans (kHard sentinels become row breaks)
    void spans( const std::vector<Span> &sp )
    {
        for( size_t i = 0; i < sp.size(); ++i )
        {
            if( sp[i].text.size() == 1 && sp[i].text[0] == kHard )
            { hardBreak(); continue; }
            emitWords( sp[i].text, sp[i].role );
        }
    }

    void codeSpans( const std::vector<Span> &sp )
    {
        for( size_t i = 0; i < sp.size(); ++i )
            addRaw( sp[i].text, sp[i].role );
    }

    void finish()
    {
        if( cw_ > 0 || line_.size() > ( line_.empty() ? 0 : 0 ) )
            if( !line_.empty() )
                emit();
        line_.clear();
    }

private:
    void emitWords( const std::string &text, unsigned char role )
    {
        size_t pos = 0;
        while( pos <= text.size() )
        {
            size_t sp = text.find( ' ', pos );
            std::string w = ( sp == std::string::npos )
                ? text.substr( pos ) : text.substr( pos, sp - pos );
            if( !w.empty() ) addWord( w, role );
            if( sp == std::string::npos ) break;
            pos = sp + 1;
        }
    }

    void splitLong( const std::string &t, unsigned char r )
    {
        size_t pos = 0;
        while( pos < t.size() )
        {
            int room = avail_ - cw_;
            if( room <= 0 ) { endLine(); room = avail_; }
            int take = (int) std::min( (size_t) room, t.size() - pos );
            line_.push_back( Span( t.substr( pos, take ), r ) );
            cw_ += take; pos += take;
        }
    }

    void startLine()
    {
        line_.clear();
        const std::string &p = first_ ? fp_ : cp_;
        unsigned char pr = first_ ? fpRole_ : cpRole_;
        if( !p.empty() ) line_.push_back( Span( p, pr ) );
        cw_ = 0;
        avail_ = ctx_.width - (int) p.size();
        if( avail_ < 1 ) avail_ = 1;
    }

    void endLine()
    {
        emit();
        first_ = false;
        startLine();
    }

    void emit() { Line l; l.spans = line_; out_.push_back( l ); }

    const Ctx &ctx_;
    std::string fp_, cp_;
    unsigned char fpRole_, cpRole_;
    std::vector<Line> &out_;
    std::vector<Span> line_;
    int avail_ = 1;
    int cw_;
    bool first_;
};

} // anonymous namespace

// --- block level ------------------------------------------------------------

namespace {

bool isBlank( const std::string &s )
{
    for( size_t i = 0; i < s.size(); ++i )
        if( !isSp( s[i] ) ) return false;
    return true;
}

bool isFence( const std::string &body, char &fc, int &len, std::string &info )
{
    if( body.empty() ) return false;
    fc = body[0];
    if( fc != '`' && fc != '~' ) return false;
    len = runLen( body, 0, body.size(), fc );
    if( len < 3 ) return false;
    info = trim( body.substr( len ) );
    if( fc == '`' && info.find( '`' ) != std::string::npos ) return false;
    return true;
}

bool isClosingFence( const std::string &line, char fc, int len )
{
    std::string b = ltrim( line );
    if( b.size() < (size_t) len || b[0] != fc ) return false;
    int r = runLen( b, 0, b.size(), fc );
    if( r < len ) return false;
    std::string rest = trim( b.substr( r ) );
    return rest.empty();
}

bool isHeading( const std::string &body, int &level )
{
    if( body.empty() || body[0] != '#' ) return false;
    int n = runLen( body, 0, body.size(), '#' );
    if( n < 1 || n > 6 ) return false;
    if( n < (int) body.size() && body[n] != ' ' && body[n] != '\t' ) return false;
    level = n;
    return true;
}

bool isThematic( const std::string &body )
{
    char c = body[0];
    if( c != '-' && c != '*' && c != '_' ) return false;
    int count = 0;
    for( size_t i = 0; i < body.size(); ++i )
    {
        if( body[i] == c ) ++count;
        else if( !isSp( body[i] ) ) return false;
    }
    return count >= 3;
}

struct Item
{
    bool ok;
    int indent;         // leading spaces of the marker
    std::string marker; // drawn marker text, e.g. "- " "1. " "[x] "
    int markerW;        // display width of marker in source
    std::string content;// text after the marker
    Item() : ok( false ), indent( 0 ), markerW( 0 ) {}
};

bool parseItem( const std::string &line, Item &it )
{
    it = Item();
    int ind = leadSpaces( line );
    if( ind > 3 ) return false;
    std::string b = line.substr( ind );
    if( b.empty() ) return false;
    // bullet
    if( ( b[0] == '-' || b[0] == '*' || b[0] == '+' ) &&
        b.size() >= 2 && isSp( b[1] ) )
    {
        it.ok = true;
        it.indent = ind;
        it.markerW = 2;
        it.marker = "- ";
        std::string rest = b.substr( 2 );
        while( !rest.empty() && rest[0] == ' ' ) rest = rest.substr( 1 ); // extra spaces
        it.content = rest;
        return true;
    }
    // ordered  1.  or  1)
    size_t i = 0;
    while( i < b.size() && std::isdigit( (unsigned char) b[i] ) ) ++i;
    if( i > 0 && i < b.size() && ( b[i] == '.' || b[i] == ')' ) &&
        i + 1 < b.size() && isSp( b[i + 1] ) )
    {
        it.ok = true;
        it.indent = ind;
        std::string num = b.substr( 0, i + 1 );
        it.marker = num + " ";
        it.markerW = (int) it.marker.size();
        std::string rest = b.substr( i + 2 );
        while( !rest.empty() && rest[0] == ' ' ) rest = rest.substr( 1 );
        it.content = rest;
        return true;
    }
    return false;
}

// task list prefix: content "[ ] x" / "[x] x"
bool parseTask( std::string &content, std::string &box )
{
    if( content.size() >= 3 && content[0] == '[' && content[2] == ']' &&
        ( content[1] == ' ' || content[1] == 'x' || content[1] == 'X' ) )
    {
        bool done = ( content[1] == 'x' || content[1] == 'X' );
        box = done ? "[x] " : "[ ] ";
        content = content.substr( 4 );
        return true;
    }
    return false;
}

// --- table helpers ----------------------------------------------------------

bool isTableDelim( const std::string &line )
{
    std::string s = trim( line );
    if( s.empty() || s.find( '-' ) == std::string::npos ) return false;
    if( s[0] != ':' && s[0] != '-' && s[0] != '|' ) return false;
    bool anyDash = false;
    for( size_t i = 0; i < s.size(); ++i )
    {
        if( s[i] == '-' ) anyDash = true;
        else if( s[i] == ':' || s[i] == '|' || isSp( s[i] ) ) {}
        else return false;
    }
    return anyDash;
}

std::vector<std::string> splitRow( const std::string &line )
{
    std::string s = trim( line );
    if( !s.empty() && s[0] == '|' ) s = s.substr( 1 );
    if( !s.empty() && s[s.size() - 1] == '|' ) s = s.substr( 0, s.size() - 1 );
    std::vector<std::string> cells;
    std::string cur;
    for( size_t i = 0; i < s.size(); ++i )
    {
        if( s[i] == '\\' && i + 1 < s.size() )
        { cur += s[i]; cur += s[i + 1]; ++i; continue; }
        if( s[i] == '|' ) { cells.push_back( trim( cur ) ); cur.clear(); }
        else cur += s[i];
    }
    cells.push_back( trim( cur ) );
    return cells;
}

enum Align { ALeft, ACenter, ARight };

std::vector<Align> parseAligns( const std::string &line )
{
    std::vector<Align> a;
    for( std::string c : splitRow( line ) )
    {
        bool l = !c.empty() && c[0] == ':';
        bool r = !c.empty() && c[c.size() - 1] == ':';
        a.push_back( l && r ? ACenter : r ? ARight : ALeft );
    }
    return a;
}

// --- forward decls ----------------------------------------------------------

void renderBlocks( const std::vector<std::string> &lines, size_t from, size_t to,
                   const Ctx &ctx, const std::string &prefix,
                   std::vector<Line> &out );

// --- emitters ---------------------------------------------------------------

void emitCode( const std::vector<std::string> &lines, size_t &i, size_t to,
               const Ctx &ctx, const std::string &prefix,
               std::vector<Line> &out )
{
    char fc; int len; std::string info;
    std::string body = ltrim( lines[i] );
    isFence( body, fc, len, info );
    Lang lang = detectLang( info );
    ++i;
    SynState st;
    std::vector<std::string> code;
    while( i < to )
    {
        if( isClosingFence( lines[i], fc, len ) ) { ++i; break; }
        std::string cl = lines[i];
        // strip up to `len`-level indentation? keep as-is minus common indent
        code.push_back( cl );
        ++i;
    }
    for( size_t k = 0; k < code.size(); ++k )
    {
        std::vector<Span> sp;
        highlightLine( lang, code[k], st, sp );
        size_t start = out.size();
        Layout lay( ctx, prefix, prefix, rNormal, rNormal, out );
        lay.codeSpans( sp );
        lay.finish();
        // A blank line inside a fence produces no spans, so Layout emits no
        // row; add an empty one so the blank line is preserved (and gets the
        // code background below) instead of collapsing the block.
        if( out.size() == start )
            out.push_back( Line() );
        // Mark every row this code line produced (wrapping may add rows) with
        // the code background so the whole row reads as code, edge-to-edge.
        for( size_t r = start; r < out.size(); ++r )
            out[r].bg = rCode;
    }
}

void emitQuote( const std::vector<std::string> &lines, size_t &i, size_t to,
                const Ctx &ctx, const std::string &prefix,
                std::vector<Line> &out )
{
    std::vector<std::string> inner;
    while( i < to )
    {
        std::string b = ltrim( lines[i] );
        if( b.empty() || b[0] != '>' ) break;
        std::string r = b.substr( 1 );
        if( !r.empty() && r[0] == ' ' ) r = r.substr( 1 );
        inner.push_back( r );
        ++i;
    }
    std::string pfx = prefix + "| ";
    renderBlocks( inner, 0, inner.size(), ctx, pfx, out );
}

void emitTable( const std::vector<std::string> &lines, size_t &i, size_t to,
                const Ctx &ctx, const std::string &prefix,
                std::vector<Line> &out )
{
    std::vector<std::string> header = splitRow( lines[i] );
    std::vector<Align> align = parseAligns( lines[i + 1] );
    size_t rows0 = i + 2;
    std::vector<std::vector<std::string> > rows;
    size_t k = rows0;
    while( k < to && !isBlank( lines[k] ) &&
            lines[k].find( '|' ) != std::string::npos )
    {
        rows.push_back( splitRow( lines[k] ) );
        ++k;
    }
    size_t ncol = header.size();
    if( align.size() < ncol ) align.resize( ncol, ALeft );

    // width per column = max display width of its cells (inline-stripped)
    std::vector<int> colw( ncol, 0 );
    auto cellWidth = [&]( const std::string &c )
    {
        std::vector<Span> sp;
        inlineRuns( c, 0, c.size(), false, false, false, false, ctx.links, sp );
        int w = 0;
        for( size_t z = 0; z < sp.size(); ++z ) w += (int) sp[z].text.size();
        return w;
    };
    for( size_t c = 0; c < ncol; ++c ) colw[c] = cellWidth( header[c] );
    for( size_t r = 0; r < rows.size(); ++r )
        for( size_t c = 0; c < ncol && c < rows[r].size(); ++c )
            colw[c] = std::max( colw[c], cellWidth( rows[r][c] ) );

    // guard total width so columns stay single-line
    int total = 0;
    for( size_t c = 0; c < ncol; ++c ) total += colw[c] + 3;
    if( total > ctx.width && ncol > 0 )
    {
        int over = total - ctx.width;
        for( int pass = 0; pass < 100 && over > 0; ++pass )
        {
            bool red = false;
            for( size_t c = 0; c < ncol && over > 0; ++c )
                if( colw[c] > 3 ) { --colw[c]; --over; red = true; }
            if( !red ) break;
        }
    }

    auto emitRow = [&]( const std::vector<std::string> &cells, bool head )
    {
        Layout lay( ctx, prefix, prefix, rNormal, rNormal, out );
        for( size_t c = 0; c < ncol; ++c )
        {
            std::string cell = c < cells.size() ? cells[c] : std::string();
            std::vector<Span> sp;
            if( head )
                sp.push_back( Span( cell, rBold ) );
            else
                inlineRuns( cell, 0, cell.size(), false, false, false, false,
                            ctx.links, sp );
            int w = 0;
            for( size_t z = 0; z < sp.size(); ++z ) w += (int) sp[z].text.size();
            int pad = colw[c] - w; if( pad < 0 ) pad = 0;
            int lead = 0;
            if( align[c] == ARight ) lead = pad;
            else if( align[c] == ACenter ) lead = pad / 2;
            int trail = pad - lead;
            if( lead )  lay.addRaw( std::string( lead, ' ' ), rNormal );
            lay.codeSpans( sp );
            if( trail ) lay.addRaw( std::string( trail, ' ' ), rNormal );
            lay.addRaw( " |", rMarker );
        }
        lay.finish();
    };

    emitRow( header, true );
    // separator
    {
        Layout lay( ctx, prefix, prefix, rNormal, rNormal, out );
        for( size_t c = 0; c < ncol; ++c )
            lay.addRaw( std::string( colw[c], '-' ) + " |", rMarker );
        lay.finish();
    }
    for( size_t r = 0; r < rows.size(); ++r )
        emitRow( rows[r], false );
    i = k;
}

// A line that begins a new block (so it must not continue a list item's
// first paragraph — e.g. a nested list starts on its own line).
bool startsNewBlock( const std::string &ln )
{
    int ind = leadSpaces( ln );
    std::string body = ln.substr( std::min<size_t>( ind, ln.size() ) );
    if( body.empty() ) return false;
    if( body[0] == '>' ) return true;
    char fc; int fl; std::string info;
    if( isFence( body, fc, fl, info ) ) return true;
    int lvl; if( isHeading( body, lvl ) ) return true;
    if( isThematic( body ) ) return true;
    Item t; if( parseItem( body, t ) ) return true;
    return false;
}

// render a run of list items starting at `from`
void emitList( const std::vector<std::string> &lines, size_t &i, size_t to,
               const Ctx &ctx, const std::string &prefix,
               std::vector<Line> &out )
{
    Item first;
    parseItem( lines[i], first );
    int base = first.indent;

    while( i < to )
    {
        if( isBlank( lines[i] ) )
        {
            size_t j = i;
            while( j < to && isBlank( lines[j] ) ) ++j;
            Item peek;
            if( j < to && parseItem( lines[j], peek ) && peek.indent >= base )
            { i = j; continue; }
            break;
        }
        Item it;
        if( !parseItem( lines[i], it ) || it.indent != base )
            break;

        std::string content = it.content;
        std::string box;
        bool task = parseTask( content, box );

        // collect continuation lines (blank or indented >= content column)
        int contentCol = base + it.markerW;
        std::vector<std::string> content2;
        content2.push_back( content );
        size_t k = i + 1;
        while( k < to )
        {
            if( isBlank( lines[k] ) )
            {
                // include blank only if a following line is indented (part of item)
                size_t j = k;
                while( j < to && isBlank( lines[j] ) ) ++j;
                if( j < to && leadSpaces( lines[j] ) >= contentCol )
                {
                    for( size_t z = k; z < j; ++z ) content2.push_back( "" );
                    content2.push_back( lines[j].substr( contentCol ) );
                    k = j + 1;
                    continue;
                }
                break;
            }
            int mind = leadSpaces( lines[k] );
            if( mind >= contentCol )
            {
                content2.push_back( lines[k].substr( contentCol ) );
                ++k;
                continue;
            }
            break;
        }
        i = k;

        // split off the first paragraph (up to first blank)
        std::vector<std::string> firstPara;
        size_t p = 0;
        while( p < content2.size() && !isBlank( content2[p] ) &&
               !( p > 0 && startsNewBlock( content2[p] ) ) )
        { firstPara.push_back( content2[p] ); ++p; }
        size_t restStart = p;

        std::string spaces( base, ' ' );
        std::string marker = spaces + ( task ? box : it.marker );
        std::string contPre = prefix + std::string( base + it.markerW, ' ' );

        // first paragraph: join lines with soft breaks, honoring hard breaks
        std::vector<Span> sp;
        for( size_t q = 0; q < firstPara.size(); ++q )
        {
            std::string ln = firstPara[q];
            bool hard = ln.size() >= 2 && ln[ln.size() - 2] == ' ' &&
                        ln[ln.size() - 1] == ' ';
            ln = rtrim( ln );
            inlineRuns( ln, 0, ln.size(), false, false, false, false,
                        ctx.links, sp );
            if( q + 1 < firstPara.size() )
                sp.push_back( Span( std::string( 1, hard ? kHard : ' ' ), rNormal ) );
        }
        Layout lay( ctx, prefix + marker, contPre, rMarker, rNormal, out );
        lay.spans( sp );
        lay.finish();

        // remaining content (extra paragraphs / nested lists) at contPre
        if( restStart < content2.size() )
        {
            std::vector<std::string> rest( content2.begin() + restStart,
                                           content2.end() );
            renderBlocks( rest, 0, rest.size(), ctx, contPre, out );
        }
    }
}

// --- the block dispatcher ---------------------------------------------------

void renderBlocks( const std::vector<std::string> &lines, size_t from, size_t to,
                   const Ctx &ctx, const std::string &prefix,
                   std::vector<Line> &out )
{
    std::vector<Span> para;
    bool hasPara = false;
    bool atHard = false;

    auto flushPara = [&]( void )
    {
        if( !hasPara ) return;
        Layout lay( ctx, prefix, prefix,
                    ( !prefix.empty() ? rQuote : rNormal ), rNormal, out );
        lay.spans( para );
        lay.finish();
        para.clear();
        hasPara = false;
        atHard = false;
    };

    size_t i = from;
    while( i < to )
    {
        const std::string &L = lines[i];

        if( isBlank( L ) ) { flushPara(); ++i; continue; }

        int ind = leadSpaces( L );
        std::string body = L.substr( std::min<size_t>( ind, L.size() ) );
        bool atTop = ( ind <= 3 );

        // fenced code
        char fc; int flen; std::string info;
        if( atTop && isFence( body, fc, flen, info ) )
        {
            flushPara();
            emitCode( lines, i, to, ctx, prefix, out );
            continue;
        }
        // heading
        int level;
        if( atTop && isHeading( body, level ) )
        {
            flushPara();
            std::string text = body.substr( level );
            text = rtrim( text );
            while( !text.empty() && text[text.size() - 1] == '#' )
                text = rtrim( text.substr( 0, text.size() - 1 ) );
            std::vector<Span> sp;
            inlineRuns( text, 0, text.size(), true, false, false, false,
                        ctx.links, sp );
            Layout lay( ctx, prefix, prefix,
                        ( !prefix.empty() ? rQuote : rNormal ), rNormal, out );
            lay.spans( sp );
            lay.finish();
            ++i;
            continue;
        }
        // thematic break
        if( atTop && isThematic( body ) )
        {
            flushPara();
            Layout lay( ctx, prefix, prefix,
                        ( !prefix.empty() ? rQuote : rNormal ), rNormal, out );
            int n = ctx.width - (int) prefix.size();
            if( n < 1 ) n = 1;
            if( n > 3 ) n -= 1;
            lay.addRaw( std::string( n, '-' ), rMarker );
            lay.finish();
            ++i;
            continue;
        }
        // blockquote
        if( atTop && !body.empty() && body[0] == '>' )
        {
            flushPara();
            emitQuote( lines, i, to, ctx, prefix, out );
            continue;
        }
        // footnote definition  [^id]: text
        if( atTop && body.size() > 2 && body[0] == '[' && body[1] == '^' )
        {
            size_t rb = body.find( "]:" );
            if( rb != std::string::npos )
            {
                flushPara();
                std::string id = body.substr( 1, rb - 1 );
                std::string text = trim( body.substr( rb + 2 ) );
                std::vector<Span> sp;
                sp.push_back( Span( "[" + id + "] ", rLink ) );
                inlineRuns( text, 0, text.size(), false, false, false, false,
                            ctx.links, sp );
                Layout lay( ctx, prefix, prefix,
                            ( !prefix.empty() ? rQuote : rNormal ), rNormal, out );
                lay.spans( sp );
                lay.finish();
                ++i;
                continue;
            }
        }
        // table
        if( atTop && body.find( '|' ) != std::string::npos && i + 1 < to &&
            !isBlank( lines[i + 1] ) && isTableDelim( lines[i + 1] ) )
        {
            flushPara();
            emitTable( lines, i, to, ctx, prefix, out );
            continue;
        }
        // list
        Item it;
        if( atTop && parseItem( body, it ) )
        {
            flushPara();
            emitList( lines, i, to, ctx, prefix, out );
            continue;
        }

        // paragraph line
        std::string content = ltrim( L );
        bool hard = content.size() >= 2 &&
                    content[content.size() - 2] == ' ' &&
                    content[content.size() - 1] == ' ';
        content = rtrim( content );
        if( hasPara && !atHard && !para.empty() )
            para.push_back( Span( std::string( 1, ' ' ), rNormal ) );
        inlineRuns( content, 0, content.size(), false, false, false, false,
                    ctx.links, para );
        if( hard ) { para.push_back( Span( std::string( 1, kHard ), rNormal ) );
                     atHard = true; }
        else atHard = false;
        hasPara = true;
        ++i;
    }
    flushPara();
}

// --- link/footnote reference-definition extraction --------------------------

void extractLinkDefs( std::vector<std::string> &lines, LinkMap &links )
{
    std::vector<std::string> keep;
    for( size_t i = 0; i < lines.size(); ++i )
    {
        std::string b = ltrim( lines[i] );
        if( b.size() > 3 && b[0] == '[' && b[1] != '^' )
        {
            size_t rb = b.find( "]:" );
            if( rb != std::string::npos )
            {
                std::string label = lower( b.substr( 1, rb - 1 ) );
                std::string rest = trim( b.substr( rb + 2 ) );
                Link lk;
                size_t sp = rest.find_first_of( " \t\"" );
                if( sp == std::string::npos ) { lk.url = rest; }
                else { lk.url = rest.substr( 0, sp );
                       std::string t = trim( rest.substr( sp ) );
                       if( t.size() >= 2 && ( t[0] == '"' || t[0] == '\'' ) )
                           lk.title = t.substr( 1, t.size() - 2 ); }
                if( !label.empty() ) links[label] = lk;
                continue;   // consume the definition line
            }
        }
        keep.push_back( lines[i] );
    }
    lines.swap( keep );
}

} // anonymous namespace

// --- public -----------------------------------------------------------------

std::vector<Line> render( const std::string &md, int width )
{
    if( width < 8 ) width = 8;
    Ctx ctx;
    ctx.width = width;

    // normalize line endings
    std::string s;
    s.reserve( md.size() );
    for( size_t i = 0; i < md.size(); ++i )
    {
        if( md[i] == '\r' )
        {
            s += '\n';
            if( i + 1 < md.size() && md[i + 1] == '\n' ) ++i;
        }
        else s += md[i];
    }

    // split into lines
    std::vector<std::string> lines;
    std::string cur;
    for( size_t i = 0; i < s.size(); ++i )
    {
        if( s[i] == '\n' ) { lines.push_back( cur ); cur.clear(); }
        else cur += s[i];
    }
    lines.push_back( cur );

    extractLinkDefs( lines, ctx.links );

    std::vector<Line> out;
    renderBlocks( lines, 0, lines.size(), ctx, "", out );
    return out;
}

} // namespace atui::tui
} // namespace atui
