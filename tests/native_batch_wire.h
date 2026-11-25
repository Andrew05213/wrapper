#pragma once

#include "../lite/httplib.h"
#include "../lite/lite.h"
#include "../lite/native_batch_http.h"
#include <cassert>
#include <thread>

// Independent fixture-side framing. These are the v1 values a downloader sees.
namespace wire {
constexpr const char* path = "/native/decrypt/batch";
constexpr const char* type = "application/vnd.wrapper.native-batch";
constexpr const char* uri = "skd://itunes.apple.com/synthetic-batch";
const std::string query = "?adamId=1&uri=skd%3A%2F%2Fitunes.apple.com%2Fsynthetic-batch";
const std::string batch_path = path + query;
const std::string single_path = "/native/decrypt" + query;

inline void put_be(std::string& bytes, uint64_t value, size_t width) {
    std::string number(width, 0);
    for (auto it = number.rbegin(); it != number.rend(); ++it, value >>= 8)
        *it = static_cast<char>(value & 255);
    bytes.append(number);
}

inline std::string request(uint64_t first, const std::string* blocks, size_t count) {
    std::string bytes;
    size_t size = 10 + 4 * count;
    for (size_t i = 0; i < count; ++i) size += blocks[i].size();
    bytes.reserve(size);
    bytes.push_back(1);
    bytes.push_back(static_cast<char>(count));
    put_be(bytes, first, 8);
    for (size_t i = 0; i < count; ++i) {
        put_be(bytes, blocks[i].size(), 4);
        bytes.append(blocks[i]);
    }
    return bytes;
}

inline uint64_t take_be(const std::string& bytes, size_t& offset, size_t width) {
    assert(bytes.size() - offset >= width);
    uint64_t value = 0;
    while (width--) value = value * 256 + static_cast<uint8_t>(bytes[offset++]);
    return value;
}

inline std::vector<std::string> reply(const httplib::Result& response, uint64_t first,
                                      size_t count, uint8_t status, size_t completed) {
    assert(response && response->status == 200);
    assert(response->get_header_value("Content-Type") == type);
    assert(response->get_header_value("Cache-Control") == "no-store");
    const auto& bytes = response->body;
    size_t offset = 0;
    assert(take_be(bytes, offset, 1) == 1);
    assert(take_be(bytes, offset, 1) == status);
    assert(take_be(bytes, offset, 1) == count);
    assert(take_be(bytes, offset, 1) == completed);
    assert(take_be(bytes, offset, 8) == first);
    std::vector<std::string> blocks;
    for (size_t i = 0; i < completed; ++i) {
        const auto len = take_be(bytes, offset, 4);
        assert(len && len % 16 == 0 && len <= bytes.size() - offset);
        blocks.emplace_back(bytes.data() + offset, len);
        offset += len;
    }
    assert(offset == bytes.size());
    return blocks;
}

inline void configure(httplib::Client& client) {
    client.set_keep_alive(true);
    client.set_tcp_nodelay(true);
    client.set_connection_timeout(5, 0);
    client.set_read_timeout(5, 0);
    client.set_write_timeout(5, 0);
}

// Exercise the actual new HTTP adapter against the legacy injected handler's
// single-block body and response shape, over private host loopback.
class Service {
public:
    Service(bool enabled, const native_batch::HttpAuthorize& authorized) {
        server_.new_task_queue = [] { return new httplib::ThreadPool(8); };
        server_.set_tcp_nodelay(true);
        server_.set_payload_max_length(1 << 20);
        server_.set_keep_alive_max_count(256);
        server_.set_keep_alive_timeout(1);
        server_.Post("/native/decrypt", [authorized](const httplib::Request& req, httplib::Response& res) {
            if (!authorized(req)) {
                res.status = 401;
                return;
            }
            std::string output;
            if (!native_decrypt(req.get_header_value("X-AMDL-Stream"), req.get_param_value("adamId"),
                                req.get_param_value("uri"), req.body, output)) {
                res.status = 502;
                return;
            }
            res.set_header("Cache-Control", "no-store");
            res.set_content(std::move(output), "application/octet-stream");
        });
        server_.Delete("/native/decrypt", [authorized](const httplib::Request& req, httplib::Response& res) {
            if (!authorized(req)) {
                res.status = 401;
                return;
            }
            native_decrypt_close(req.get_header_value("X-AMDL-Stream"));
            res.set_content("{}", "application/json");
        });
        assert(native_batch::register_http_routes(server_, enabled, authorized) == enabled);
        port_ = server_.bind_to_any_port("127.0.0.1");
        assert(port_ > 0);
        thread_ = std::thread([this] { assert(server_.listen_after_bind()); });
        server_.wait_until_ready();
    }

    ~Service() {
        server_.stop();
        thread_.join();
    }

    int port() const { return port_; }

private:
    httplib::Server server_;
    int port_;
    std::thread thread_;
};
} // namespace wire
