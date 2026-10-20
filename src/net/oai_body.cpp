// Definitions for the chat-completion JSON.  See oai_body.h.
#include "oai_body.h"

#include "json_extract.h"      // jsonEscape, ToolCallFrag
#include "core/strutil.h"      // toString: Watcom has no std::to_string

namespace atui {
namespace net {


// Merge a streaming delta.tool_calls fragment batch into the response, keyed
// by the fragment index (id/name arrive once, arguments arrive in pieces).
void mergeToolFrags( OaiResponse &resp, const std::vector<ToolCallFrag> &frags )
{
    for( size_t i = 0; i < frags.size(); ++i )
    {
        const ToolCallFrag &f = frags[i];
        int idx = f.index < 0 ? 0 : f.index;
        while( (int) resp.toolCalls.size() <= idx )
            resp.toolCalls.push_back( OaiToolCall() );
        OaiToolCall &tc = resp.toolCalls[idx];
        if( !f.id.empty() ) tc.id = f.id;
        if( !f.name.empty() && tc.name.empty() ) tc.name = f.name;
        tc.arguments += f.arguments;
    }
}

void applyStreamDelta( const std::string &payload, OaiResponse *out,
                       DeltaSink *sink )
{
    const std::string content = deltaContent( payload );
    if( !content.empty() )
    {
        if( sink )
            sink->onDelta( content, false );
        if( out )
            out->content += content;
    }

    const std::string reasoning = deltaReasoning( payload );
    if( !reasoning.empty() )
    {
        if( sink )
            sink->onDelta( reasoning, true );
        if( out )
            out->reasoning += reasoning;
    }

    if( out )
    {
        const std::string fr = finishReason( payload );
        if( !fr.empty() )
            out->finishReason = fr;

        std::vector<ToolCallFrag> frags;
        if( parseToolCallFrags( payload, frags ) )
            mergeToolFrags( *out, frags );
    }
}

// Build the JSON request body for /v1/chat/completions.
std::string buildBody( const OaiRequest &req )
{
    std::string body;
    body.reserve( 256 );
    body += "{\"model\":\"";
    body += jsonEscape( req.model );
    body += "\",\"stream\":";
    body += ( req.stream ? "true" : "false" );
    body += ",\"temperature\":";
    body += core::toString( req.temperature );
    // Only sent when the caller asked for a cap: servers disagree about the
    // default and some reject a field they do not implement, so an ordinary chat
    // request stays exactly as it was.
    if( req.maxTokens > 0 )
    {
        body += ",\"max_tokens\":";
        body += core::toString( req.maxTokens );
    }
    // Servers disagree about how to say "skip the chain of thought", so all three
    // spellings go out and the ones that are not understood are ignored.
    if( req.disableThinking )
    {
        body += ",\"reasoning_effort\":\"none\"";
        body += ",\"think\":false";
        body += ",\"chat_template_kwargs\":{\"enable_thinking\":false}";
    }

    body += ",\"messages\":[";
    for( size_t i = 0; i < req.messages.size(); ++i )
    {
        const OaiMessage &m = req.messages[i];
        if( i > 0 ) body += ", ";
        body += "{\"role\":\"";
        body += jsonEscape( m.role );
        body += "\",\"content\":";
        // An assistant turn that only asks for tools may carry no text; emit
        // null content then (some servers reject empty strings with tool_calls).
        bool nullContent = ( m.role == "assistant" && !m.toolCalls.empty() &&
                             m.content.empty() );
        if( nullContent )
            body += "null";
        else
        {
            body += "\"";
            body += jsonEscape( m.content );
            body += "\"";
        }

        // Optional thinking text carried on an assistant turn (only present
        // when the user enabled "resend thinking"). Emitted as
        // reasoning_content, the common replay convention.
        if( m.role == "assistant" && !m.reasoning.empty() )
        {
            body += ",\"reasoning_content\":\"";
            body += jsonEscape( m.reasoning );
            body += "\"";
        }

        if( m.role == "assistant" && !m.toolCalls.empty() )
        {
            body += ",\"tool_calls\":[";
            for( size_t c = 0; c < m.toolCalls.size(); ++c )
            {
                const OaiToolCall &tc = m.toolCalls[c];
                if( c ) body += ",";
                body += "{\"id\":\"";
                body += jsonEscape( tc.id );
                body += "\",\"type\":\"function\",\"function\":{\"name\":\"";
                body += jsonEscape( tc.name );
                body += "\",\"arguments\":\"";
                body += jsonEscape( tc.arguments ); // arguments is itself JSON text
                body += "\"}}";
            }
            body += "]";
        }

        if( m.role == "tool" )
        {
            body += ",\"tool_call_id\":\"";
            body += jsonEscape( m.toolCallId );
            body += "\"";
            if( !m.name.empty() )
            {
                body += ",\"name\":\"";
                body += jsonEscape( m.name );
                body += "\"";
            }
        }
        body += "}";
    }
    body += "]";

    if( !req.toolsJson.empty() )
    {
        body += ",\"tools\":";
        body += req.toolsJson;
        body += ",\"tool_choice\":\"auto\"";
    }
    body += "}";
    return body;
}

} // namespace atui::net
} // namespace atui
