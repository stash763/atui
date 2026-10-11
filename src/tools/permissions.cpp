// Implementation of the permission policy. See permissions.h.
//
// Two rules hold this file together. First: anything that cannot be settled with
// facts is asked about, never allowed. Second: the checks here are the boundary
// and the permission agent is only evidence handed to them - so an agent that is
// hostile, confused or unreachable can widen what reaches the user and nothing
// more.
#include "permissions.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <direct.h>
#include <sys/stat.h>
#endif

namespace atui {
namespace tools {

namespace {

// --- small string helpers ------------------------------------------------------

std::string trimmed( const std::string &s )
{
    const std::size_t a = s.find_first_not_of( " \t\r\n" );
    if( a == std::string::npos ) return std::string();
    const std::size_t b = s.find_last_not_of( " \t\r\n" );
    return s.substr( a, b - a + 1 );
}

std::string lowered( const std::string &s )
{
    std::string o( s );
    for( std::size_t i = 0; i < o.size(); ++i )
        o[i] = (char) std::tolower( (unsigned char) o[i] );
    return o;
}

bool startsWith( const std::string &s, const char *p )
{
    return s.compare( 0, std::strlen( p ), p ) == 0;
}

bool endsWith( const std::string &s, const char *p )
{
    const std::size_t n = std::strlen( p );
    return s.size() >= n && s.compare( s.size() - n, n, p ) == 0;
}

const char *homeDir()
{
    const char *h = std::getenv( "HOME" );
    return ( h && h[0] ) ? h : "";
}

// No trailing slash, so "root + '/' + x" and the prefix comparisons below are
// unambiguous. A root of "/" keeps its slash - rootTooBroad() is what handles it.
std::string stripTrailingSlash( std::string p )
{
    while( p.size() > 1 && p[p.size() - 1] == '/' ) p.erase( p.size() - 1 );
    return p;
}

// --- paths ---------------------------------------------------------------------

// Collapses ".", ".." and doubled slashes textually, for the part of a path that
// does not exist yet and so cannot be consulted.
std::string normalizePath( const std::string &path )
{
    const bool absolute = ( !path.empty() && path[0] == '/' );
    std::vector< std::string > parts;

    std::string::size_type a = 0;
    while( a <= path.size() )
    {
        std::string::size_type b = path.find( '/', a );
        if( b == std::string::npos ) b = path.size();
        const std::string part = path.substr( a, b - a );
        if( part == ".." )
        {
            if( !parts.empty() && parts.back() != ".." ) parts.pop_back();
            else if( !absolute ) parts.push_back( ".." );
            // ".." above the root is simply the root.
        }
        else if( !part.empty() && part != "." )
            parts.push_back( part );

        if( b == path.size() ) break;
        a = b + 1;
    }

    std::string out;
    for( std::size_t i = 0; i < parts.size(); ++i )
    {
        if( i || absolute ) out += "/";
        out += parts[i];
    }
    if( out.empty() ) out = absolute ? "/" : ".";
    return out;
}

// Resolves symlinks for as much of `absolute` as exists and appends the rest
// unchanged: creating a file is a write like any other, so a path that does not
// exist yet still has to be placed inside or outside the tree.
std::string resolvePath( const std::string &absolute )
{
#if defined(_WIN32)
    return normalizePath( absolute );
#else
    char buf[ 4096 ];
    if( ::realpath( absolute.c_str(), buf ) ) return std::string( buf );

    std::string rest;
    std::string head = absolute;
    for( ;; )
    {
        const std::string::size_type slash = head.find_last_of( '/' );
        if( slash == std::string::npos ) break;
        const std::string part = head.substr( slash + 1 );
        if( !part.empty() ) rest = rest.empty() ? part : part + "/" + rest;
        head = head.substr( 0, slash );
        if( head.empty() ) head = "/";
        if( ::realpath( head.c_str(), buf ) )
        {
            std::string out( buf );
            if( !rest.empty() )
            {
                if( out != "/" ) out += "/";
                out += rest;
            }
            return out;
        }
        if( head == "/" ) break;
    }
    return normalizePath( absolute );
#endif
}

std::string cwdPath()
{
    char buf[ 4096 ];
#if !defined(_WIN32)
    if( ::getcwd( buf, sizeof buf ) ) return std::string( buf );
#else
    if( ::_getcwd( buf, sizeof buf ) ) return std::string( buf );
#endif
    return std::string();
}

// The working directory, symlinks resolved: what "the project" means unless
// --root says otherwise.
std::string resolvedCwd()
{
    const std::string cwd = cwdPath();
    if( cwd.empty() ) return std::string();
    return stripTrailingSlash( resolvePath( normalizePath( cwd ) ) );
}

// Absolute, expanded, resolved form of `path`, or empty when it cannot be placed.
std::string absolutePath( const std::string &path )
{
    std::string p = trimmed( path );
    if( p.empty() ) return std::string();

    if( p[0] == '~' )
    {
        const char *h = homeDir();
        if( !h[0] ) return std::string();
        p = std::string( h ) + p.substr( 1 );
    }
    if( p[0] != '/' )
    {
        const std::string root = projectRoot();
        if( root.empty() ) return std::string();
        p = ( root == "/" ? std::string() : root ) + "/" + p;
    }
    return resolvePath( normalizePath( p ) );
}

// Whole-component match: ".ssh" matches "/u/.ssh/id" but not "/u/.sshagent".
// `pattern` may itself be several components (".config/gh").
bool hasComponents( const std::string &path, const char *pattern )
{
    const std::size_t n = std::strlen( pattern );
    if( path.empty() || n == 0 ) return false;
    std::size_t at = 0;
    while( ( at = path.find( pattern, at ) ) != std::string::npos )
    {
        const bool leftOk  = ( at == 0 || path[at - 1] == '/' );
        const bool rightOk = ( at + n == path.size() || path[at + n] == '/' );
        if( leftOk && rightOk ) return true;
        ++at;
    }
    return false;
}

std::string baseName( const std::string &path )
{
    const std::string::size_type slash = path.find_last_of( '/' );
    return ( slash == std::string::npos ) ? path : path.substr( slash + 1 );
}

// Word-boundary search, so "eval" does not match "retrieval" while "/bin/sh"
// still matches "sh": a path separator is a boundary, and running a shell by its
// absolute path is the same thing.
bool hasToken( const std::string &s, const char *token )
{
    const std::size_t n = std::strlen( token );
    if( s.empty() || n == 0 ) return false;
    std::size_t at = 0;
    while( ( at = s.find( token, at ) ) != std::string::npos )
    {
        const char left  = ( at == 0 ) ? ' ' : s[at - 1];
        const char right = ( at + n == s.size() ) ? ' ' : s[at + n];
        const bool word = !( std::isalnum( (unsigned char) left ) ||
                             left == '_' );
        const bool wordEnd = !( std::isalnum( (unsigned char) right ) ||
                                right == '_' );
        if( word && wordEnd ) return true;
        ++at;
    }
    return false;
}

// --- what must never run unattended --------------------------------------------

// Directories whose contents either execute later or are the keys to the
// machine. Auto-approving a write into .git is not a file edit: the next git
// command runs whatever landed there.
const char *kDeniedComponents[] = {
    ".git", ".hg", ".svn", ".ssh", ".aws", ".gnupg", ".kube", ".docker",
    ".vscode", ".idea", ".config/gh", ".config/gcloud", "keychain",
    "keychains", ".mozilla"
};

// Files that are secrets, or that a shell runs when it starts.
const char *kDeniedFiles[] = {
    ".netrc", ".npmrc", ".pypirc", ".gitconfig", ".git-credentials",
    ".bashrc", ".bash_profile", ".bash_login", ".profile", ".zshrc",
    ".zshenv", ".zprofile", ".zsh_history", ".bash_history"
};

const char *kDeniedSuffixes[] = {
    ".pem", ".key", ".p12", ".pfx", ".kdbx", ".crt", ".der", ".secret"
};

const char *kDeniedPrefixes[] = {
    "id_rsa", "id_dsa", "id_ecdsa", "id_ed25519"
};

// --- the surface this policy refuses to reason about ---------------------------

// Anywhere in the command: the effect depends on data the text does not contain
// (substitution, expansion), the payload is encoded, or another program is
// started with arguments the scanner cannot follow.
const char *kOpaqueTokens[] = {
    "eval", "exec", "sudo", "doas", "su", "xargs", "base64", "base32", "xxd"
};

// As the consumer of a pipe: the piped data becomes code. Interpreters are only
// suspicious here - `python3 -m pytest` is the ordinary work this feature exists
// to stop interrupting.
const char *kPipedInto[] = {
    "sh", "bash", "zsh", "ksh", "csh", "fish", "python", "python3", "perl",
    "ruby", "node", "php", "awk", "expect"
};

// True when any of `paths` lands outside the project; `first` returns it.
bool anyOutside( const std::vector< std::string > &paths,
                 std::string &first )
{
    for( std::size_t i = 0; i < paths.size(); ++i )
    {
        if( !pathInsideProject( paths[i] ) )
        {
            first = paths[i];
            return true;
        }
    }
    return false;
}

bool anyDenyListed( const std::vector< std::string > &paths,
                    std::string &path, std::string &why )
{
    for( std::size_t i = 0; i < paths.size(); ++i )
    {
        if( denyListed( paths[i], why ) )
        {
            path = paths[i];
            return true;
        }
    }
    return false;
}

} // anonymous namespace

// --- configuration -------------------------------------------------------------

PermissionMode &permissionMode()
{
    // The historical behaviour, and the right fallback everywhere the agent is
    // not available: asking is always correct, just not always welcome.
    // settings::loadAll() raises this to pmAgent, which is also the default a
    // network build wants.
    static PermissionMode mode = pmAskAll;
    return mode;
}

NetworkPolicy &networkPolicy()
{
    static NetworkPolicy policy = npAgent;
    return policy;
}

int &judgeSeconds()
{
    static int seconds = kDefaultJudgeSeconds;
    return seconds;
}

bool &logDecisions()
{
    static bool enabled = true;
    return enabled;
}

std::string &decisionLogPath()
{
    // Empty means "nowhere". settings::loadAll() points it beside the config
    // file, so a test run that never loads settings leaves no files behind.
    static std::string path;
    return path;
}

std::string &judgeModelOverride()
{
    static std::string model;
    return model;
}

const char *permissionModeName( PermissionMode mode )
{
    switch( mode )
    {
        case pmExact:  return "exact";
        case pmAgent:  return "agent";
        case pmAskAll: break;
    }
    return "ask_all";
}

bool permissionModeFromName( const std::string &name, PermissionMode &out )
{
    const std::string n = lowered( trimmed( name ) );
    if( n == "ask_all" || n == "ask" ) { out = pmAskAll; return true; }
    if( n == "exact" )                 { out = pmExact;  return true; }
    if( n == "agent" )                 { out = pmAgent;  return true; }
    return false;
}

const char *networkPolicyName( NetworkPolicy policy )
{
    switch( policy )
    {
        case npAsk:   return "ask";
        case npAllow: return "allow";
        case npAgent: break;
    }
    return "agent";
}

bool networkPolicyFromName( const std::string &name, NetworkPolicy &out )
{
    const std::string n = lowered( trimmed( name ) );
    if( n == "agent" ) { out = npAgent; return true; }
    if( n == "ask" )   { out = npAsk;   return true; }
    if( n == "allow" ) { out = npAllow; return true; }
    return false;
}

int clampJudgeSeconds( int seconds )
{
    if( seconds < kMinJudgeSeconds ) return kMinJudgeSeconds;
    if( seconds > kMaxJudgeSeconds ) return kMaxJudgeSeconds;
    return seconds;
}

// --- the project root ----------------------------------------------------------

const std::string &projectRoot()
{
    static std::string root = resolvedCwd();
    return root;
}

void setProjectRoot( const std::string &root )
{
    // Resolved exactly like the default, so --root on a path that goes through a
    // symlink (macOS /tmp again) compares equal to the resolved cwd.
    std::string wanted = trimmed( root );
    if( wanted.empty() )
    {
        wanted = resolvedCwd();
    }
    else
    {
        if( wanted[0] == '~' )
        {
            const char *h = homeDir();
            if( h[0] ) wanted = std::string( h ) + wanted.substr( 1 );
        }
        if( wanted[0] != '/' )
        {
            const std::string cwd = resolvedCwd();
            if( cwd.empty() ) return;
            wanted = cwd + "/" + wanted;
        }
        wanted = stripTrailingSlash( resolvePath( normalizePath( wanted ) ) );
    }

    // Written once at startup (or by a test), read many times afterwards - the
    // same process-wide-singleton shape as the rest of the settings.
    std::string &slot = const_cast< std::string & >( projectRoot() );
    slot = wanted;
}

bool rootTooBroad()
{
    const std::string root = projectRoot();
    if( root.empty() || root == "/" ) return true;
    const char *h = homeDir();
    if( !h[0] ) return false;
    return stripTrailingSlash( resolvePath( normalizePath( h ) ) ) == root;
}

// --- facts ---------------------------------------------------------------------

bool pathInsideProject( const std::string &path )
{
    const std::string root = projectRoot();
    if( root.empty() ) return false;

    const std::string abs = absolutePath( path );
    if( abs.empty() ) return false;
    if( abs == root ) return true;
    if( abs.size() > root.size() &&
        abs.compare( 0, root.size(), root ) == 0 )
        return abs[root.size()] == '/';   // /u/proj-extra is not in /u/proj
    return false;
}

void pathsMentioned( const std::string &text,
                     std::vector< std::string > &out )
{
    // Split on everything a shell uses to separate words or redirect, then keep
    // the tokens that look like paths. Nothing else is inferred: whether `make`
    // writes anywhere is a question for the agent, whereas whether the text
    // contains "/etc/hosts" is a fact.
    const std::size_t kMaxPaths = 32;
    const char *separators = " \t\r\n;|&<>(){}\"'`";

    std::string::size_type at = 0;
    while( at < text.size() && out.size() < kMaxPaths )
    {
        const std::size_t end = text.find_first_of( separators, at );
        std::string token = text.substr(
            at, ( end == std::string::npos ? std::string::npos : end - at ) );
        at = ( end == std::string::npos ) ? text.size() : end + 1;

        // "--out=/tmp/x" or "OUT=/tmp/x": the path is the value.
        const std::string::size_type eq = token.find_last_of( '=' );
        if( eq != std::string::npos ) token = token.substr( eq + 1 );

        if( token.size() < 2 ) continue;
        const bool pathLike =
            token[0] == '/' || token[0] == '.' || token[0] == '~' ||
            token.find( '/' ) != std::string::npos;
        if( !pathLike ) continue;

        if( std::find( out.begin(), out.end(), token ) == out.end() )
            out.push_back( token );
    }
}

bool unverifiedSurface( const std::string &command, std::string &why )
{
    // Each check is a shape whose effect depends on something the text does not
    // contain. Such a command is neither auto-approved nor sent to the agent:
    // this is exactly what an injected instruction writes, and the only honest
    // answer is to show it to a person.
    static const char *kSubstitution[] = { "$(", "`", "${" };
    for( std::size_t i = 0; i < sizeof kSubstitution / sizeof kSubstitution[0]; ++i )
    {
        if( command.find( kSubstitution[i] ) != std::string::npos )
        {
            why = "it substitutes a command or variable in place";
            return true;
        }
    }
    if( command.find( "-exec" ) != std::string::npos ||
        command.find( "-ok" ) != std::string::npos )
    {
        why = "it runs a program per match";
        return true;
    }
    for( std::size_t i = 0; i < sizeof kOpaqueTokens / sizeof kOpaqueTokens[0]; ++i )
    {
        if( hasToken( command, kOpaqueTokens[i] ) )
        {
            why = std::string( "it uses " ) + kOpaqueTokens[i];
            return true;
        }
    }
    if( command.find( '|' ) != std::string::npos )
    {
        for( std::size_t i = 0; i < sizeof kPipedInto / sizeof kPipedInto[0]; ++i )
        {
            if( hasToken( command, kPipedInto[i] ) )
            {
                why = "it pipes data into an interpreter";
                return true;
            }
        }
    }
    return false;
}

bool denyListed( const std::string &path, std::string &why )
{
    const std::string raw = trimmed( path );
    if( raw.empty() ) return false;

    // Both forms are checked. The resolved one catches a symlink inside the tree
    // pointing at ~/.ssh; the raw one catches a path that cannot be resolved yet
    // but is plainly .env.
    const std::string resolved = absolutePath( raw );
    const std::string candidates[] = { raw, resolved };

    for( int c = 0; c < 2; ++c )
    {
        const std::string p = lowered( candidates[c] );
        if( p.empty() ) continue;

        for( std::size_t i = 0;
             i < sizeof kDeniedComponents / sizeof kDeniedComponents[0]; ++i )
        {
            if( hasComponents( p, kDeniedComponents[i] ) )
            {
                why = std::string( "in " ) + kDeniedComponents[i];
                return true;
            }
        }

        const std::string name = baseName( p );
        if( name.empty() ) continue;

        if( name == ".env" || startsWith( name, ".env." ) )
        {
            why = "a dotenv file";
            return true;
        }
        for( std::size_t i = 0;
             i < sizeof kDeniedFiles / sizeof kDeniedFiles[0]; ++i )
        {
            if( name == kDeniedFiles[i] )
            {
                why = name;
                return true;
            }
        }
        for( std::size_t i = 0;
             i < sizeof kDeniedSuffixes / sizeof kDeniedSuffixes[0]; ++i )
        {
            if( name.size() > std::strlen( kDeniedSuffixes[i] ) &&
                endsWith( name, kDeniedSuffixes[i] ) )
            {
                why = std::string( "a " ) + kDeniedSuffixes[i] + " file";
                return true;
            }
        }
        for( std::size_t i = 0;
             i < sizeof kDeniedPrefixes / sizeof kDeniedPrefixes[0]; ++i )
        {
            if( startsWith( name, kDeniedPrefixes[i] ) )
            {
                why = name;
                return true;
            }
        }
        // Editor task files: read by the editor, which then runs what is in them.
        if( name == "tasks.json" )
        {
            why = "an editor task file";
            return true;
        }
    }
    return false;
}

// --- the decision --------------------------------------------------------------

Permission decide( const PermissionSubject &subject, const EffectPlan *plan )
{
    Permission p;

    // 1. The mode asks for everything; nothing below is consulted.
    if( permissionMode() == pmAskAll )
    {
        p.outcome = poAsk;
        p.rung    = prModeAskAll;
        p.reason  = "asking before every tool call";
        return p;
    }

    // 2. A root wide enough to be everything is not a boundary, and "inside the
    //    project" would approve the user's whole home directory.
    if( rootTooBroad() )
    {
        p.outcome = poAsk;
        p.rung    = prRootTooBroad;
        p.reason  = "the project directory is your home directory";
        return p;
    }

    // 3. A tool whose call cannot be inspected is never run unattended - an
    //    unknown effect is the definition of not-knowing.
    if( subject.kind == psUnknown || trimmed( subject.value ).empty() )
    {
        p.outcome = poAsk;
        p.rung    = prUnknownCall;
        p.reason  = "what this tool would do cannot be checked";
        return p;
    }

    // The facts, computed once. For a file these are the answer; for a command
    // they are the parts of the answer that do not need asking anybody.
    std::vector< std::string > paths;
    if( subject.kind == psPath ) paths.push_back( subject.value );
    else pathsMentioned( subject.value, paths );

    // 4. Hooks, credentials, editor tasks: never unattended, in the tree or out.
    std::string deniedPath, deniedWhy;
    if( anyDenyListed( paths, deniedPath, deniedWhy ) )
    {
        p.outcome = poAsk;
        p.rung    = prDenyList;
        p.reason  = "never done unattended (" + deniedWhy + ")";
        p.detail  = deniedPath;
        return p;
    }

    if( subject.kind == psPath )
    {
        // 5. A file is decidable exactly, which is why no agent is consulted.
        if( pathInsideProject( subject.value ) )
        {
            p.outcome = poRun;
            p.rung    = prPathInside;
            p.reason  = "inside the project";
            p.detail  = subject.value;
            return p;
        }
        p.outcome = poAsk;
        p.rung    = prPathOutside;
        p.reason  = "outside the project";
        p.detail  = subject.value;
        return p;
    }

    // 6. A command with surface this policy will not reason about.
    std::string why;
    if( unverifiedSurface( subject.value, why ) )
    {
        p.outcome = poAsk;
        p.rung    = prUnverified;
        p.reason  = "too indirect to check automatically - " + why;
        return p;
    }

    // 7. Any path it names that lands outside the tree.
    std::string outside;
    if( anyOutside( paths, outside ) )
    {
        p.outcome = poAsk;
        p.rung    = prPathOutside;
        p.reason  = "reaches outside the project";
        p.detail  = outside;
        return p;
    }

    // 8. The facts have run out. Without an agent - or in a mode that does not
    //    use one - what is left is a question for the user.
    if( permissionMode() != pmAgent )
    {
        p.outcome = poAsk;
        p.rung    = prNoAgent;
        p.reason  = "nothing outside the project is involved, but it could not "
                    "be checked further";
        return p;
    }
    if( !plan )
    {
        p.outcome = poAsk;
        p.rung    = prNoAgent;
        p.reason  = "no permission agent was consulted";
        return p;
    }

    // 9. An answer that could not be read is not an answer.
    if( !plan->parsed )
    {
        p.outcome = poAsk;
        p.rung    = prPlanUnread;
        p.reason  = "the permission agent could not describe the command";
        return p;
    }

    // 10. Effects that are never safe to start without a person.
    if( plan->elevated )
    {
        p.outcome = poAsk;
        p.rung    = prPlanFlagged;
        p.reason  = "it asks for elevated privileges";
        return p;
    }
    if( plan->sensitive )
    {
        p.outcome = poAsk;
        p.rung    = prPlanFlagged;
        p.reason  = "it reads personal data";
        return p;
    }
    if( plan->exec )
    {
        p.outcome = poAsk;
        p.rung    = prPlanFlagged;
        p.reason  = "it runs downloaded or obfuscated code";
        return p;
    }

    // 11. Network together with a read outside the project is exfiltration, and
    //     no setting overrides it: `judge_network = allow` says fetching
    //     dependencies is fine, not that shipping the user's files somewhere is.
    if( plan->net )
    {
        for( std::size_t i = 0; i < plan->reads.size(); ++i )
        {
            std::string unusedWhy;
            const std::string &read = plan->reads[i];
            if( !pathInsideProject( read ) )
            {
                p.outcome = poAsk;
                p.rung    = prExfil;
                p.reason  = "it reaches the network and reads outside the "
                            "project";
                p.detail  = read;
                return p;
            }
            if( denyListed( read, unusedWhy ) )
            {
                p.outcome = poAsk;
                p.rung    = prExfil;
                p.reason  = "it reaches the network and reads a file that is "
                            "never safe to send";
                p.detail  = read;
                return p;
            }
        }
        if( networkPolicy() == npAsk )
        {
            p.outcome = poAsk;
            p.rung    = prPlanFlagged;
            p.reason  = "it reaches the network (permissions network: ask)";
            return p;
        }
    }

    // 12. Writes we cannot place inside the project - including any write the
    //     agent described in a form that is not a path at all.
    for( std::size_t i = 0; i < plan->writes.size(); ++i )
    {
        if( !pathInsideProject( plan->writes[i] ) )
        {
            p.outcome = poAsk;
            p.rung    = prWriteOutside;
            p.reason  = "it writes outside the project";
            p.detail  = plan->writes[i];
            return p;
        }
    }

    // 13. Described, contained, and nothing was flagged.
    p.outcome = poRun;
    p.rung    = prPlanCleared;
    p.reason  = plan->summary.empty() ? std::string( "the permission agent "
                              "found nothing outside the project" )
                                      : plan->summary;
    return p;
}

std::string decisionNote( const Permission &p )
{
    if( p.outcome == poRun )
        return std::string( p.rung == prPlanCleared ? "agent: " : "auto: " ) +
               p.reason;

    std::string note = p.reason;
    if( !p.detail.empty() )
    {
        note += ": ";
        note += p.detail;
    }
    return note;
}

// --- the decision log ----------------------------------------------------------

namespace {

std::mutex &logMutex()
{
    static std::mutex m;
    return m;
}

// Log lines are `key=value` so they are greppable by hand and parseable by a
// script, with the free text last. Quotes, backslashes and newlines are escaped
// and everything non-printable becomes '?', which keeps one line per decision.
std::string logEscape( const std::string &s, std::size_t maxBytes )
{
    std::string out;
    for( std::size_t i = 0; i < s.size() && out.size() < maxBytes; ++i )
    {
        const char c = s[i];
        if( c == '\n' )      out += "\\n";
        else if( c == '\t' ) out += "\\t";
        else if( c == '"' )  out += "\\\"";
        else if( c == '\\' ) out += "\\\\";
        else if( (unsigned char) c < 32 || (unsigned char) c > 126 ) out += '?';
        else out += c;
    }
    if( s.size() > maxBytes ) out += "...";
    return out;
}

std::string logTimestamp()
{
    char buf[ 32 ];
    const std::time_t now = std::time( 0 );
    std::tm utc;
#if !defined(_WIN32)
    ::gmtime_r( &now, &utc );
#else
    utc = *::gmtime( &now );
#endif
    std::strftime( buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &utc );
    return std::string( buf );
}

// Keeps the log from growing without limit on its own: one rotation, one
// generation back. Anyone who wants history keeps it out of the app's way.
const long kMaxLogBytes = 1024 * 1024;

void rotateIfNeeded( const std::string &path )
{
#if !defined(_WIN32)
    struct stat st;
    if( ::stat( path.c_str(), &st ) != 0 ) return;
    if( st.st_size < kMaxLogBytes ) return;
    std::string bak = path + ".1";
    std::remove( bak.c_str() );
    ::rename( path.c_str(), bak.c_str() );
#endif
}

} // anonymous namespace

void logDecision( const std::string &toolName, const Permission &p,
                  const std::string &agent, long agentMs,
                  const std::string &preview )
{
    if( !logDecisions() ) return;
    const std::string path = decisionLogPath();
    if( path.empty() ) return;

    std::string line = logTimestamp();
    line += " tool=";   line += logEscape( toolName, 40 );
    line += " decision=";
    line += ( p.outcome == poRun ) ? "run"
          : ( p.outcome == poAgent ) ? "agent" : "ask";
    line += " rung=";
    {
        char num[ 16 ];
        std::snprintf( num, sizeof num, "%d", p.rung );
        line += num;
    }
    line += " mode=";
    line += permissionModeName( permissionMode() );
    line += " agent=";   line += logEscape( agent.empty() ? "-" : agent, 60 );
    {
        char ms[ 24 ];
        std::snprintf( ms, sizeof ms, " latency=%ldms", agentMs );
        line += ms;
    }
    if( !p.reason.empty() )
    {
        line += " reason=\"";
        line += logEscape( p.reason, 160 );
        line += "\"";
    }
    if( !p.detail.empty() )
    {
        line += " detail=\"";
        line += logEscape( p.detail, 160 );
        line += "\"";
    }
    line += " cmd=\"";
    line += logEscape( preview, 400 );
    line += "\"\n";

    // Written on the worker thread while the UI thread may be clearing settings,
    // and opened per entry: a handful of lines per turn, nothing buffered, so a
    // crash cannot lose a decision that was already acted on.
    std::lock_guard< std::mutex > lock( logMutex() );
    rotateIfNeeded( path );
    std::FILE *f = std::fopen( path.c_str(), "ab" );
    if( !f ) return;
    std::fwrite( line.data(), 1, line.size(), f );
    std::fclose( f );
}

} // namespace atui::tools
} // namespace atui
