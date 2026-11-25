#pragma once

#include <functional>

namespace httplib {
class Server;
struct Request;
}

namespace native_batch {

using HttpAuthorize = std::function<bool(const httplib::Request&)>;

// Private service startup hook. The embedder supplies its current session and
// gateway authorization check. Neither the stock server nor public routes call it.
bool register_http_routes(httplib::Server& server, bool enabled, const HttpAuthorize& authorized);

} // namespace native_batch
