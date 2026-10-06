// Definitions for the OpenAI-compatible chat model (threaded SSE).
//
// A single ask() may involve several round-trips: if the model replies with a
// tool call (finish_reason == "tool_calls"), we execute the requested tool(s),
// append the assistant + tool-result turns, and ask again so the model can
// produce a final natural-language answer. All of this runs on the worker
// thread; a status line is pushed to the queue so the UI shows tool activity.
#include "oai_chat_model.h"
#include "oai_client.h"
#include "provider.h"
#include "tools/tool_registry.h"
#ifdef ATUI_DEBUG_KEYS
#include "dbg.h"
#endif

#include <cstring>
#include <memory>

namespace atui {
namespace net {

namespace {

struct CbCtx {
    TokenQueue *queue;
};

// C-style callback invoked by libcurl on the worker thread: forward each
// streamed content token to the UI queue for live display.
void pushToken( const char *tok, void *ctx )
{
    if( !tok || !ctx ) return;
    CbCtx *c = static_cast< CbCtx * >( ctx );
    if( c->queue )
        c->queue->push( std::string( tok, std::strlen( tok ) ) );
}

// Guard against a model that keeps requesting tools without ever finishing.
const int kMaxToolRounds = 6;

} // anonymous namespace

OaiChatModel::OaiChatModel()
    : running_( false ),
      done_( false )
{
    // Endpoint settings live in the process-wide providerSettings() so the
    // Options > Providers dialog can retarget a remote llama.cpp server.
}

OaiChatModel::~OaiChatModel()
{
    // An in-flight worker (detached) thread is not joined here; the app is
    // expected to outlive any request (hard exit reaps the thread).
}

bool OaiChatModel::ask( const std::string &userMessage, const std::string &history,
                        TokenQueue *queue )
{
    ( void )history; // this model keeps its own conversation
    if( !queue || running_ )
        return false;

    atui::tools::registerDefaults();

    // Snapshot the provider settings so a dialog edit mid-request cannot
    // race with this request (and the next message picks up the change).
    const ProviderSettings cfg = providerSettings();

    // Start from the current conversation plus this user message. Nothing is
    // written to conv_ yet; the worker commits the whole exchange on success,
    // so conv_ always mirrors exactly what the model has actually seen.
    std::vector< OaiMessage > msgs;
    {
        std::lock_guard< std::mutex > lock( convMutex_ );
        msgs = conv_;
    }
    msgs.push_back( OaiMessage( "user", userMessage ) );

    // Tools offered to this request (rendered once; stable across rounds).
    const std::string toolsJson =
        atui::tools::ToolRegistry::instance().renderToolsJson();

    running_ = true;
    done_ = false;

    std::thread worker( [ this, msgs, queue, cfg, toolsJson ]() mutable {
        bool ok = true;

        for( int round = 0; round < kMaxToolRounds; ++round )
        {
            OaiRequest req;
            req.model = cfg.model;
            req.temperature = cfg.temperature;
            req.stream = true;
            req.messages = msgs;
            req.toolsJson = toolsJson;

            OaiResponse resp;
            CbCtx cctx;
            cctx.queue = queue;

            OaiClient client;
            ok = client.request( cfg.baseUrl, cfg.apiKey, req,
                                 pushToken, &cctx, &resp );
#ifdef ATUI_DEBUG_KEYS
            atui::net::dbg( "worker round %d: ok=%d content=%zu tools=%zu finish='%s'",
                            round, (int)ok, resp.content.size(),
                            resp.toolCalls.size(), resp.finishReason.c_str() );
#endif
            if( !ok )
                break;

            // Record the assistant turn (text and/or tool calls).
            OaiMessage asst;
            asst.role = "assistant";
            asst.content = resp.content;
            asst.toolCalls = resp.toolCalls;
            msgs.push_back( asst );

            // Not a tool call: this was the final answer.
            if( !( resp.finishReason == "tool_calls" && !resp.toolCalls.empty() ) )
                break;

            // Execute each requested tool and append its result, then loop so
            // the model can turn the results into a final answer.
            for( size_t i = 0; i < resp.toolCalls.size(); ++i )
            {
                const OaiToolCall &tc = resp.toolCalls[i];

                if( queue )
                    queue->push( "\n~ calling " + tc.name + "\n" );

                atui::tools::Tool *tool =
                    atui::tools::ToolRegistry::instance().find( tc.name );
                std::string result = tool
                    ? tool->execute( tc.arguments )
                    : ( "error: unknown tool '" + tc.name + "'" );

                OaiMessage tr;
                tr.role = "tool";
                tr.toolCallId = tc.id;
                tr.name = tc.name;
                tr.content = result;
                msgs.push_back( tr );
            }
        }

        // Commit the whole exchange only on success so a failed turn does not
        // pollute history. Safe to overwrite conv_: the running_ flag prevents
        // a second ask() while this worker is active.
        if( ok )
        {
            std::lock_guard< std::mutex > lock( convMutex_ );
            conv_ = msgs;
        }

        running_ = false;
        done_ = true;
    } );

    worker.detach();
    return true;
}

} // namespace atui::net
} // namespace atui
