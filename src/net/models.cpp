// The model list: `GET /models`, and the ids inside it.
//
// The Providers dialog asks for this while it is being edited, which is a caller with
// no event loop to step -- so it goes through the blocking face of the transport
// (`httpGet`) rather than the state machine the chat path uses.  Same framing, same
// deadlines, same rule about `https`.
//
// Declaration stays in `oai_client.h`, where its callers already find it.
#include "oai_client.h"

#include "http_simple.h"
#include "json_extract.h"

namespace atui {
namespace net {

bool fetchModels( const std::string &baseUrl,
                  const std::string &apiKey,
                  std::vector<std::string> &models,
                  std::string &err )
{
    models.clear();
    err.clear();
    if( baseUrl.empty() )
    {
        err = "no server URL set";
        return false;
    }

    std::string body;
    if( !httpGet( baseUrl, "models", apiKey, 20, body, err ) )
        return false;

    // An empty list is an answer, not a failure: a server with no models loaded is
    // a real state, and the dialog says "0 model(s) loaded" for it, which is the
    // truth.  Turning it into an error would hide the difference between a server
    // that answered and one that did not.
    models = modelIds( body );
    return true;
}

} // namespace atui::net
} // namespace atui
