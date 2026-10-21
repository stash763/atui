// Helpers for scripting an OpenAI-compatible answer over `ScriptedHttp`.
//
// The pieces every test that talks to a chat endpoint needs: an SSE body for an
// answer, one for a tool call, and the HTTP around them with the length actually
// computed rather than remembered.  Kept here because two layers (ChatRequest and
// ChatTurn) are tested against the same shape of reply, and a bug in how a tool call
// is scripted would otherwise show up as a mystery failure in whichever file did not
// own the helper.
#ifndef ATUI_TESTS_SSE_SCRIPT_H
#define ATUI_TESTS_SSE_SCRIPT_H

#include <cstdio>
#include <string>

namespace atui_test {

// JSON string escaping, for values that carry quotes of their own -- a tool call's
// `arguments` is JSON text inside a JSON string, which is where hand-built fixtures
// usually go wrong.
inline std::string esc( const std::string &in )
{
    std::string out;
    for( std::size_t i = 0; i < in.size(); ++i )
    {
        const char c = in[i];
        if( c == '"' )       out += "\\\"";
        else if( c == '\\' ) out += "\\\\";
        else if( c == '\n' ) out += "\\n";
        else                 out += c;
    }
    return out;
}

// One `delta` payload, with an optional finish reason on the same choice.
inline std::string deltaJson( const std::string &field, const std::string &value,
                              const std::string &finish = std::string() )
{
    return "data: {\"choices\":[{\"delta\":{\""+ field + "\":\"" + esc( value ) + "\"}" +
           ( finish.empty() ? std::string()
                            : ",\"finish_reason\":\"" + finish + "\"" ) +
           "}]}\n\n";
}

// A whole answer: one content delta, `stop`, and the end of the stream.
inline std::string sseAnswer( const std::string &text )
{
    return deltaJson( "content", text, "stop" ) + "data: [DONE]\n\n";
}

// An answer that asks for one tool call, then a second payload with the finish
// reason: the shape a server sends, where the call and the reason arrive apart.
inline std::string sseToolCall( const std::string &id, const std::string &name,
                                const std::string &arguments )
{
    return std::string( "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
                        "\"id\":\"" ) + esc( id ) + "\",\"function\":{\"name\":\"" + esc( name ) +
           "\",\"arguments\":\"" + esc( arguments ) + "\"}}]}}]}\n\n" +
           "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n" +
           "data: [DONE]\n\n";
}

// An answer that says nothing and finishes: no tool calls, no text.
inline std::string sseFinished( const std::string &reason = "stop" )
{
    return "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"" + reason +
           "\"}]}\n\n" + "data: [DONE]\n\n";
}

// A chunked HTTP 200 wrapping an SSE body, which is what these servers send.
inline std::string sseResponse( const std::string &body,
                                const std::string &status = "200 OK" )
{
    char head[ 48 ];
    std::snprintf( head, sizeof head, "HTTP/1.1 %s\r\nTransfer-Encoding: chunked\r\n\r\n",
                   status.c_str() );
    char size[ 24 ];
    std::snprintf( size, sizeof size, "%lx\r\n", (unsigned long) body.size() );
    return std::string( head ) + size + body + "\r\n" + "0\r\n\r\n";
}

// The same without chunking: `Content-Length`, computed.  Both framings are in the
// wild, and a length that does not match the body is a truncated answer, not a
// shorter one.
inline std::string sseResponseFixed( const std::string &body,
                                     const std::string &status = "200 OK" )
{
    char head[ 96 ];
    std::snprintf( head, sizeof head,
                   "HTTP/1.1 %s\r\nContent-Length: %lu\r\n\r\n", status.c_str(),
                   (unsigned long) body.size() );
    return std::string( head ) + body;
}

// A refusal, with the server's own explanation as the body.
inline std::string errorResponse( const std::string &status,
                                  const std::string &body )
{
    return sseResponseFixed( body, status );
}

} // namespace atui_test

#endif // ATUI_TESTS_SSE_SCRIPT_H
