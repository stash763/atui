// Implementation of the prompt file and its editor. See core/promptfile.h.
#include "core/promptfile.h"

#include "core/hostenv.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace atui {
namespace core {

std::string &promptsEditorOverride()
{
    static std::string editor;
    return editor;
}

std::string promptsEditor()
{
    if( !promptsEditorOverride().empty() )
        return promptsEditorOverride();
    const char *env = std::getenv( "EDITOR" );
    if( env != 0 && env[ 0 ] != '\0' )
        return env;
#if defined( __OS2__ )
    return "e.exe";
#elif defined( _WIN32 )
    return "notepad.exe";
#else
    return "vi";
#endif
}

bool ensurePromptsFile( const std::string &path, std::string &error )
{
    std::FILE *f = std::fopen( path.c_str(), "rb" );
    if( f != 0 )
    {
        std::fclose( f );
        return true;
    }
    f = std::fopen( path.c_str(), "wb" );
    if( f == 0 )
    {
        error = "cannot create \"" + path + "\"";
        return false;
    }
    std::fclose( f );
    return true;
}

bool loadPromptsFile( const std::string &path, std::string &error )
{
    std::FILE *f = std::fopen( path.c_str(), "rb" );
    if( f == 0 )
    {
        error = "cannot read \"" + path + "\"";
        return false;
    }

    AgentPrompts &p = agentPrompts();
    p.extra.clear();

    std::string line;
    std::size_t count = 0;
    for ( ; ; )
    {
        line.erase();
        int c;
        for ( ; ; )
        {
            c = std::fgetc( f );
            if( c == EOF )
                break;
            if( c == '\n' )
                break;
            line += (char)c;
        }
        const bool eof = ( c == EOF );
        // CRLF files from Windows-side editors carry the CR; the line
        // is kept even when empty, so the file's own paragraphing
        // shows in the preview. buildSystemMessage drops blanks from
        // what the model reads.
        if( !line.empty() && line[ line.size() - 1 ] == '\r' )
            line.erase( line.size() - 1 );
        if( eof && line.empty() )
            break;                        // no phantom last line
        p.extra.push_back( line );
        ++count;
        if( count == 500 )
        {
            // A file this long stopped being instructions; reading
            // any more of it into every request is the user's
            // mistake, not the parser's.
            break;
        }
        if( eof )
            break;
    }
    std::fclose( f );
    return true;
}

bool openPromptsEditor( const std::string &path, std::string &error )
{
    if( !ensurePromptsFile( path, error ) )
        return false;

    std::string cmd = promptsEditor() + " ";
#if defined( __unix__ ) || defined( __APPLE__ )
    cmd += quotePosixArg( path );
#else
    cmd += quoteCmdArg( path );
#endif

    // system() through the platform's shell: on Unix it waits for
    // the editor and the reload below is the whole story; on OS/2
    // and Windows cmd starts the editor's window and returns, and
    // the dialog's Reload button is the second half.
    if( std::system( cmd.c_str() ) == -1 )
    {
        error = "could not start the editor (" + promptsEditor() + ")";
        return false;
    }
#if defined( __unix__ ) || defined( __APPLE__ )
    std::string ignored;
    loadPromptsFile( path, ignored );
#endif
    return true;
}

std::string quotePosixArg( const std::string &arg )
{
    // The single-quote sandwich: close, escaped quote, reopen. It is
    // the only shell-quoting idiom that needs no second level of
    // escaping -- backticks, dollar signs and exclamation points all
    // ride through untouched.
    std::string out = "'";
    for( std::string::size_type i = 0; i != arg.size(); ++i )
    {
        if( arg[ i ] == '\'' )
            out += "'\\''";
        else
            out += arg[ i ];
    }
    out += "'";
    return out;
}

std::string quoteCmdArg( const std::string &arg )
{
    if( arg.empty() )
        return "\"\"";
    if( arg[ 0 ] == '"' )
        return arg;                       // already wrapped, trust it
    if( arg.find( ' ' ) == std::string::npos
        && arg.find( '\t' ) == std::string::npos )
        return arg;                       // no need, no noise
    return "\"" + arg + "\"";
}

std::string fitPreviewLine( const std::string &line, std::size_t width )
{
    if( width == 0 )
        return std::string();
    if( line.size() <= width )
        return line;
    if( width == 1 )
        return "~";
    return line.substr( 0, width - 1 ) + "~";
}

} // namespace atui::core
} // namespace atui
