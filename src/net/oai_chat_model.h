// OpenAI-compatible chat model (M2). Threaded SSE request.
// C++14, network-only (ATUI_HAVE_NETWORK).
#ifndef ATUI_NET_OAI_CHAT_MODEL_H
#define ATUI_NET_OAI_CHAT_MODEL_H

#include "net/oai_client.h"
#include "net/token_queue.h"

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>

namespace atui {
namespace net {

// Endpoint settings are read from the shared providerSettings() singleton
// (seeded from ATUI_MODEL / ATUI_BASE_URL / ATUI_API_KEY / ATUI_TEMPERATURE
// and editable via the Options > Providers dialog) at each request.
class OaiChatModel {
public:
    OaiChatModel();
    ~OaiChatModel();

    // Start a streaming request. Returns true if a request was started,
    // false if the model is busy (a previous request is still in flight).
    // Generated tokens are pushed to `queue` (on the worker thread); when the
    // stream completes isDone() reports true.
    bool ask( const std::string &userMessage, const std::string &history,
              TokenQueue *queue );

    bool isDone() const { return done_; }
    bool isRunning() const { return running_; }

private:
    std::vector< OaiMessage > conv_;
    std::mutex convMutex_;
    std::atomic< bool > running_;
    std::atomic< bool > done_;
};

} // namespace atui::net
} // namespace atui

#endif // ATUI_NET_OAI_CHAT_MODEL_H
