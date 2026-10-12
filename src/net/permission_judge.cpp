// Implementation of the OpenAI-backed permission agent. See permission_judge.h.
#include "permission_judge.h"

#include "oai_client.h"
#include "provider.h"
#include "tools/permissions.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace atui {
namespace net {

namespace {

// The question. Short on purpose: this runs in front of every tool call the
// facts cannot settle, so a slow or chatty reader is a delay the user feels on
// each one. Note what is *not* here - the conversation. Whatever was said to the
// main model must not get a vote on the gate, or a prompt injection only has to
// convince one model to write a convincing line for the other.
const char *kSystemPrompt =
    "You are a permission gate for an AI coding assistant. You never run anything "
    "and you never decide: you describe one tool call in fixed lines, and a "
    "policy decides.\n"
    "Answer with lines from this set and nothing else. No prose, no markdown, no "
    "code fence.\n"
    "  READ <path>        a file or directory the call reads\n"
    "  WRITE <path>       a file or directory it creates or changes\n"
    "  NET <yes|no>       whether it uses the network\n"
    "  EXEC <yes|no>      whether it runs code from outside the project\n"
    "  ELEVATED <yes|no>  whether it needs root or changes privileges\n"
    "  SENSITIVE <yes|no> whether it reads personal data such as keys, "
    "credentials, mail, browser files or a keychain\n"
    "  SUMMARY <short line> what the call does, for a human scanning a log\n"
    "EXEC is only yes for code the project did not already contain: downloaded, "
    "piped in, decoded, or hidden from the user. Building, testing or running the "
    "project's own source is EXEC no - that is the ordinary work being described.\n"
    "Write paths exactly as the call spells them, so the policy can resolve "
    "them. Paths relative to the project directory stay relative. Omit a line "
    "that does not apply. Describe only the call shown.";

// A pathological argument blob (a huge edit) must not become a huge prompt. Only
// commands reach here in practice - a file call is decided by its path - but the
// size of a tool argument is not something this module controls.
const std::size_t kMaxArgumentBytes = 4000;

std::string trimmed( const std::string &s )
{
    const std::size_t a = s.find_first_not_of( " \t\r\n" );
    if( a == std::string::npos ) return std::string();
    const std::size_t b = s.find_last_not_of( " \t\r\n" );
    return s.substr( a, b - a + 1 );
}

std::string upperKey( const std::string &s )
{
    std::string o;
    for( std::size_t i = 0; i < s.size(); ++i )
        o += (char) std::toupper( (unsigned char) s[i] );
    return o;
}

// Drops the decoration models add around a list: bullets, a leading quote, the
// backticks of a code fence.
std::string stripDecorations( const std::string &line )
{
    std::string s = trimmed( line );
    while( s.size() >= 2 && ( s[0] == '-' || s[0] == '*' || s[0] == '>' ) &&
           ( s[1] == ' ' || s[1] == '\t' ) )
        s = trimmed( s.substr( 2 ) );
    if( s.size() > 1 && s[0] == '`' && s[s.size() - 1] == '`' )
        s = trimmed( s.substr( 1, s.size() - 2 ) );
    return s;
}

// Strips characters a model likes to hang off the end of a path.
std::string cleanPath( std::string s )
{
    s = trimmed( s );
    while( !s.empty() && ( s[s.size() - 1] == ',' || s[s.size() - 1] == ';' ||
                           s[s.size() - 1] == '.' || s[s.size() - 1] == ':' ||
                           s[s.size() - 1] == ')' ) )
        s.erase( s.size() - 1 );
    if( s.size() > 1 && ( ( s[0] == '"' && s[s.size() - 1] == '"' ) ||
                          ( s[0] == '\'' && s[s.size() - 1] == '\'' ) ) )
        s = s.substr( 1, s.size() - 2 );
    return s;
}

// Yes/no in the shapes a model produces. Anything unreadable counts as set: the
// call mentioned the effect, and guessing "no" is the direction that runs
// something nobody approved.
bool flagSet( const std::string &value, bool &recognised )
{
    std::string v = upperKey( trimmed( value ) );
    if( v == "Y" || v == "YES" || v == "TRUE" || v == "1" ||
        v == "REQUIRED" || v == "ALWAYS" )
    {
        recognised = true;
        return true;
    }
    if( v == "N" || v == "NO" || v == "FALSE" || v == "0" || v == "NONE" ||
        v == "NOT" || v == "NEVER" || v.empty() )
    {
        recognised = true;
        return false;
    }
    recognised = false;
    return true;
}

const std::size_t kMaxPlanPaths = 32;

// ATUI_JUDGE_DEBUG=1 echoes what the model actually said to stderr. Without it
// the judgement is invisible: the UI shows "the agent could not describe the
// command" and there is no way to find out why, which makes the whole subsystem
// impossible to tune. It goes to stderr, not the transcript - it is a debugging
// aid, not something to scroll back through mid-task.
void judgeDebug( const std::string &toolName, const std::string &reply,
                 const std::string &error )
{
    const char *on = std::getenv( "ATUI_JUDGE_DEBUG" );
    if( !on || !*on || *on == '0' ) return;
    std::fprintf( stderr, "[judge %s] %s%s%s\n%s\n[/judge]\n",
                  toolName.c_str(),
                  error.empty() ? "reply:" : "error:",
                  error.empty() ? "" : " ",
                  error.empty() ? "" : error.c_str(),
                  reply.empty() ? "(empty)" : reply.c_str() );
    std::fflush( stderr );
}

} // anonymous namespace

std::string permissionAgentModel()
{
    const std::string &configured = atui::tools::judgeModelOverride();
    if( !configured.empty() ) return configured;
    return atui::net::providerSettings().model;
}

void parseEffectPlan( const std::string &reply, atui::tools::EffectPlan &plan )
{
    std::vector< std::string > lines;
    std::size_t at = 0;
    while( at <= reply.size() )
    {
        const std::size_t nl = reply.find( '\n', at );
        lines.push_back( reply.substr(
            at, ( nl == std::string::npos ? std::string::npos : nl - at ) ) );
        if( nl == std::string::npos ) break;
        at = nl + 1;
    }

    int recognised = 0;
    for( std::size_t i = 0; i < lines.size(); ++i )
    {
        const std::string line = stripDecorations( lines[i] );
        if( line.empty() || line[0] == '#' ) continue;

        // "NET: yes", "NET yes" and "net\tyes" all mean the same thing.
        const std::size_t sep = line.find_first_of( " :\t" );
        const std::string key =
            upperKey( sep == std::string::npos ? line : line.substr( 0, sep ) );
        const std::string value =
            ( sep == std::string::npos ) ? std::string()
                                         : trimmed( line.substr( sep + 1 ) );
        if( key.empty() ) continue;

        if( key == "READ" || key == "WRITE" )
        {
            const std::string path = cleanPath( value );
            if( !path.empty() )
            {
                std::vector< std::string > &list =
                    ( key == "READ" ) ? plan.reads : plan.writes;
                if( list.size() < kMaxPlanPaths ) list.push_back( path );
                ++recognised;
            }
            continue;
        }

        if( key == "SUMMARY" )
        {
            plan.summary = trimmed( value );
            ++recognised;
            continue;
        }

        // The four yes/no keys, through a member pointer so a new flag is one
        // line here and one line in EffectPlan.
        typedef atui::tools::EffectPlan Plan;   // (the member pointers below)
        bool Plan::*flag = 0;
        if( key == "NET" )            flag = &Plan::net;
        else if( key == "EXEC" )      flag = &Plan::exec;
        else if( key == "ELEVATED" )  flag = &Plan::elevated;
        else if( key == "SENSITIVE" ) flag = &Plan::sensitive;
        if( !flag ) continue;   // a key we do not have: ignore it, do not guess

        bool readable = false;
        plan.*flag = flagSet( value, readable );
        if( readable ) ++recognised;
    }

    // An answer with nothing recognisable in it is not a description of
    // anything, and an undescribed call is never run.
    plan.parsed = ( recognised > 0 );
}

void OaiPermissionJudge::plan( const std::string &toolName,
                               const std::string &argumentsJson,
                               atui::tools::EffectPlan &out )
{
    out = atui::tools::EffectPlan();   // fail closed until an answer says otherwise

    const std::string model = permissionAgentModel();
    const atui::net::ProviderSettings provider = atui::net::providerSettings();
    if( model.empty() || provider.baseUrl.empty() ) return;

    std::string ask = "Project directory: " + atui::tools::projectRoot() + "\n" +
                      "Tool: " + toolName + "\n" +
                      "Arguments: " +
                          argumentsJson.substr( 0, kMaxArgumentBytes ) + "\n" +
                      "Describe this call.";

    OaiRequest req;
    req.model          = model;
    req.temperature    = 0.0;
    // Streamed, because that is the one shape every server here answers: a
    // non-streaming reply is a single JSON body and this client reads SSE lines.
    // The answer is accumulated rather than shown, so streaming costs nothing.
    req.stream         = true;
    req.maxTokens      = 300;
    req.timeoutSeconds = atui::tools::judgeSeconds();
    // Without this, a reasoning model uses the whole budget on thinking and
    // answers with nothing - see OaiRequest::disableThinking.
    req.disableThinking = true;
    req.messages.push_back( OaiMessage( "system", kSystemPrompt ) );
    req.messages.push_back( OaiMessage( "user", ask ) );

    OaiResponse resp;
    OaiClient client;
    std::string err;
    if( !client.request( provider.baseUrl, provider.apiKey, req, 0, 0, &resp,
                         &err ) )
    {
        // No answer - a timeout, an unreachable server, a model that does not
        // exist. Left empty on purpose: the caller reports that the agent could
        // not describe the call, and the user is asked.
        judgeDebug( toolName, "", err );
        return;
    }

    judgeDebug( toolName, resp.content, "" );
    parseEffectPlan( resp.content, out );

    if( !out.parsed && !resp.reasoning.empty() )
    {
        // A model that thinks out loud and runs out of budget before answering
        // still described the call, just in the other field. Reading it is the
        // same kind of thing the parser was built for - it is a description, and
        // `decide()` is what acts on it - and the alternative is asking the user
        // about a command that was described in full one field away.
        parseEffectPlan( resp.reasoning, out );
        judgeDebug( toolName, resp.reasoning, "" );
    }
}

OaiPermissionJudge &oaiPermissionJudge()
{
    static OaiPermissionJudge judge;
    return judge;
}

} // namespace atui::net
} // namespace atui
