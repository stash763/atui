// Implementation of the edit_file tool. See edit_file_tool.h.
#include "tools/edit_file_tool.h"
#include "core/journal.h"
#include "core/paths.h"
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
    // Truncate-and-write rather than write-a-sibling-and-rename. The rename
    // dance is the reflex, and it does not buy what it appears to buy here:
    // removing the original and renaming over it is atomic on POSIX and not at
    // all on FAT, which is half the platforms atui targets, and it leaves
    // orphaned temporary files behind when the middle of it fails. The window
    // this leaves - part-written file, process gone - is exactly what the undo
    // journal covers, and it is covered before this function is called.
    //
    // `FILE*` rather than a stream so the bytes can be asked to reach the device.
    // The journal is about to say "saved" about this file, and the only honest
    // version of that is once what it describes is past the point where losing
    // power would lose it.
    std::FILE *f = std::fopen( path.c_str(), "wb" );
    if( !f ) return false;
    bool ok = ( std::fwrite( data.data(), 1, data.size(), f ) == data.size() );
    core::flushToDisk( f );
    if( std::fclose( f ) != 0 ) ok = false;
    return ok;
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

    // Before the write, in this order, because each step is cheaper than the next
    // and none of them may be skipped: refuse a file that cannot be written,
    // then put the current contents where they survive the attempt, then write.
    if( !atui::core::isWritableFile( path ) )
        return "edit_file: '" + path + "' is not writable. No changes made.";

    atui::core::journal::Pending pend;
    std::string jerr;
    const std::string op = std::string( "replace:" ) + ( all ? "all" : "first" );
    if( !atui::core::journal::begin( path, "edit_file", op, pend, jerr ) )
    {
        // No copy, no edit. The policy normally asks a person about this before
        // the call is made (tools::prNoUndoNet); reaching here means the journal
        // failed since, and the honest answer is to leave the file alone.
        return "edit_file: not edited - " + jerr;
    }

    if( !writeFileAll( path, updated ) )
    {
        // Whether the original survived depends on where it failed, and that is
        // not knowable from here. So the entry stays pending rather than being
        // cancelled: the next start offers the copy, which is the only safe bet
        // about a state atui cannot read.
        std::string r = "edit_file: failed to write '" + path + "'";
        if( !pend.preImage.empty() )
            r += "; the previous contents are saved at '" + pend.preImage + "'";
        else
            r += ". No changes made.";
        return r;
    }

    std::string cerr;
    if( !atui::core::journal::commit( pend, cerr ) )
    {
        // The edit is done. Its undo record did not finish, which leaves the
        // entry pending and offered as an interrupted write - the safe
        // description of it. The model is told, because "edited" on its own
        // would be a half-truth.
        std::ostringstream w;
        w << "edit_file: edited '" << path
          << "' but could not record how to undo it: " << cerr;
        if( !all && found > 1 )
            w << "; " << ( found - 1 ) << " more match(es) remain";
        return w.str() + ".";
    }

    std::ostringstream r;
    r << "edit_file: edited '" << path << "' (replaced " << replaced
      << " occurrence";
    if( replaced != 1 ) r << "s";
    r << ")";
    if( !all && found > 1 )
        r << "; " << ( found - 1 )
          << " more match(es) remain — pass all=true to replace them";
    r << ".";
    if( pend.overCap )
        r << " (no undo copy: file is larger than the journal limit)";
    return r.str();
}

PermissionSubject EditFileTool::permissionSubject(
    const std::string &argumentsJson ) const
{
    std::string path;
    jsonStringArg( argumentsJson, "path", path );
    // An empty path stays psPath with no value: the policy reports that as
    // "cannot be checked" and asks, which is right for a call that names nothing.
    return PermissionSubject( psPath, path );
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

std::string EditFileTool::callPreview( const std::string &argumentsJson ) const
{
    std::string path, oldText, newText;
    bool all = false;
    jsonStringArg( argumentsJson, "path", path );
    jsonStringArg( argumentsJson, "old_text", oldText );
    jsonStringArg( argumentsJson, "new_text", newText );
    jsonBoolArg( argumentsJson, "all", all );

    std::string s = "edit_file \"";
    s += path.empty() ? "?" : clip( path, 120 );
    s += "\"";
    s += all ? " (all matches)" : " (first match)";
    if( !oldText.empty() || !newText.empty() )
    {
        s += "\n- old: \"" + clip( oldText, 160 ) + "\"";
        s += "\n+ new: \"" + clip( newText, 160 ) + "\"";
    }
    return s;
}

EditFileTool g_editFileTool;

} // namespace atui::tools
} // namespace atui
