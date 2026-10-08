// Implementation of the edit_file tool. See edit_file_tool.h.
#include "tools/edit_file_tool.h"
#include "tools/json_args.h"

#include <fstream>
#include <sstream>
#include <string>

namespace atui {
namespace tools {

namespace {

bool readFile( const std::string &path, std::string &out )
{
    std::ifstream f( path.c_str(), std::ios::in | std::ios::binary );
    if( !f ) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFileAll( const std::string &path, const std::string &data )
{
    std::ofstream f( path.c_str(), std::ios::out | std::ios::binary
                                  | std::ios::trunc );
    if( !f ) return false;
    f.write( data.data(), (std::streamsize) data.size() );
    f.flush();
    return f.good();
}

// Non-overlapping count of `from` in `src`.
int countOccurrences( const std::string &src, const std::string &from )
{
    int n = 0;
    std::size_t pos = 0;
    while( ( pos = src.find( from, pos ) ) != std::string::npos )
    {
        ++n;
        pos += from.size();
    }
    return n;
}

// Replace the first (or all) occurrences; sets `replaced`.
std::string replace( const std::string &src, const std::string &from,
                     const std::string &to, bool all, int &replaced )
{
    std::string out;
    std::size_t pos = 0;
    replaced = 0;
    while( true )
    {
        std::size_t f = src.find( from, pos );
        if( f == std::string::npos )
        {
            out.append( src, pos, std::string::npos );
            break;
        }
        out.append( src, pos, f - pos );
        out.append( to );
        pos = f + from.size();
        ++replaced;
        if( !all )
        {
            out.append( src, pos, std::string::npos );
            break;
        }
    }
    return out;
}

std::string clip( const std::string &s, std::size_t max )
{
    std::string flat;
    for( std::size_t i = 0; i < s.size() && flat.size() < max; ++i )
    {
        char c = s[i];
        if( c == '\n' ) flat += "\\n";
        else if( c == '\t' ) flat += "\\t";
        else if( (unsigned char) c < 32 ) flat += '?';
        else flat += c;
    }
    if( s.size() > max ) flat += "...";
    return flat;
}

} // anonymous namespace

const char *EditFileTool::name() const
{
    return "edit_file";
}

const char *EditFileTool::description() const
{
    return "Edit an existing text file by replacing an exact substring. "
           "Provide the file path, the exact text to find (old_text) and the "
           "replacement (new_text). By default only the first match is "
           "replaced; set all=true to replace every occurrence. The file must "
           "exist and old_text must match byte-for-byte (including "
           "whitespace).";
}

const char *EditFileTool::parametersSchema() const
{
    return "{\"type\":\"object\",\"properties\":{"
           "\"path\":{\"type\":\"string\",\"description\":\"Path to the "
           "existing file to edit\"},"
           "\"old_text\":{\"type\":\"string\",\"description\":\"Exact text to "
           "find\"},"
           "\"new_text\":{\"type\":\"string\",\"description\":\"Replacement "
           "text\"},"
           "\"all\":{\"type\":\"boolean\",\"description\":\"Replace every "
           "occurrence (default false = first only)\"}"
           "},\"required\":[\"path\",\"old_text\",\"new_text\"]}";
}

std::string EditFileTool::execute( const std::string &argumentsJson )
{
    if( !looksJsonObject( argumentsJson ) )
        return "edit_file: arguments must be a JSON object";

    std::string path, oldText, newText;
    bool all = false;
    jsonStringArg( argumentsJson, "path", path );
    jsonStringArg( argumentsJson, "old_text", oldText );
    jsonStringArg( argumentsJson, "new_text", newText );
    jsonBoolArg( argumentsJson, "all", all );

    if( path.empty() )
        return "edit_file: 'path' is required";
    if( oldText.empty() )
        return "edit_file: 'old_text' must not be empty";

    std::string data;
    if( !readFile( path, data ) )
        return "edit_file: cannot read file '" + path + "'";

    int found = countOccurrences( data, oldText );
    if( found == 0 )
        return "edit_file: 'old_text' not found in '" + path
               + "'. No changes made.";

    int replaced = 0;
    std::string updated = replace( data, oldText, newText, all, replaced );

    if( !writeFileAll( path, updated ) )
        return "edit_file: failed to write '" + path + "'";

    std::ostringstream r;
    r << "edit_file: edited '" << path << "' (replaced " << replaced
      << " occurrence";
    if( replaced != 1 ) r << "s";
    r << ")";
    if( !all && found > 1 )
        r << "; " << ( found - 1 )
          << " more match(es) remain — pass all=true to replace them";
    r << ".";
    return r.str();
}

std::string EditFileTool::approvalSummary( const std::string &argumentsJson ) const
{
    std::string path, oldText, newText;
    bool all = false;
    jsonStringArg( argumentsJson, "path", path );
    jsonStringArg( argumentsJson, "old_text", oldText );
    jsonStringArg( argumentsJson, "new_text", newText );
    jsonBoolArg( argumentsJson, "all", all );

    std::string s = "Edit file \"" + ( path.empty() ? "?" : path ) + "\"";
    s += all ? " — replace ALL matches:" : " — replace first match:";
    s += "\n  - old: " + clip( oldText, 80 );
    s += "\n  + new: " + clip( newText, 80 );
    return s;
}

EditFileTool g_editFileTool;

} // namespace atui::tools
} // namespace atui
