#include "native_batch_http.h"
#include "native_batch.h"
#include "httplib.h"
#include <array>

namespace native_batch {
namespace {

constexpr const char* path = "/native/decrypt/batch";
constexpr const char* content_type = "application/vnd.wrapper.native-batch";
constexpr size_t request_header = 10;
constexpr size_t max_body = request_header + 4 * max_blocks + max_bytes;

struct Frame {
    std::array<Block, max_blocks> blocks;
    uint64_t first_block;
    size_t count;
};

uint64_t read_number(const char* bytes, size_t width) {
    uint64_t value = 0;
    for (size_t i = 0; i < width; ++i) value = (value << 8) | static_cast<unsigned char>(bytes[i]);
    return value;
}

void append_number(std::string& bytes, uint64_t value, size_t width) {
    for (size_t i = width; i > 0; --i) bytes.push_back(static_cast<char>(value >> ((i - 1) * 8)));
}

bool decode(const std::string& body, Frame& frame) {
    if (body.size() < request_header || static_cast<uint8_t>(body[0]) != version) return false;
    frame.count = static_cast<uint8_t>(body[1]);
    if (!frame.count || frame.count > max_blocks) return false;
    frame.first_block = read_number(body.data() + 2, 8);
    size_t offset = request_header;
    for (size_t i = 0; i < frame.count; ++i) {
        if (body.size() - offset < 4) return false;
        const auto size = read_number(body.data() + offset, 4);
        offset += 4;
        if (size > body.size() - offset) return false;
        frame.blocks[i] = {body.data() + offset, static_cast<size_t>(size)};
        offset += size;
    }
    return offset == body.size();
}

std::string encode(const Frame& request, const Result& result) {
    size_t size = 12;
    for (const auto& block : result.output) size += 4 + block.size();
    std::string body;
    body.reserve(size);
    body.push_back(version);
    body.push_back(static_cast<char>(result.status));
    body.push_back(static_cast<char>(request.count));
    body.push_back(static_cast<char>(result.output.size()));
    append_number(body, request.first_block, 8);
    for (const auto& block : result.output) {
        append_number(body, block.size(), 4);
        body.append(block);
    }
    return body;
}

void reject(httplib::Response& res, int status, const char* body) {
    res.status = status;
    res.set_header("Connection", "close"); // A rejected POST may have an unread body.
    res.set_content(body, "application/json");
}

bool check_access(const httplib::Request& req, const HttpAuthorize& authorized) {
    try { return authorized(req); }
    catch (...) { return false; }
}

bool preflight(const httplib::Request& req, httplib::Response& res, const HttpAuthorize& authorized) {
    res.set_header("Cache-Control", "no-store");
    if (req.remote_addr != "127.0.0.1" || req.local_addr != "127.0.0.1") {
        reject(res, 404, R"({"code":"native_batch_unavailable"})");
        return false;
    }
    if (!check_access(req, authorized)) {
        reject(res, 401, R"({"code":"native_batch_unauthorized"})");
        return false;
    }
    return true;
}

bool read_body(const httplib::Request& req, const httplib::ContentReader& reader, std::string& body) {
    if (req.get_header_value_count("Content-Length") != 1 || req.has_header("Transfer-Encoding") ||
        req.has_header("Content-Encoding") || req.has_header("Range") ||
        req.get_header_value_count("Content-Type") != 1 || req.get_header_value("Content-Type") != content_type)
        return false;
    size_t length = 0;
    for (char ch : req.get_header_value("Content-Length")) {
        if (ch < '0' || ch > '9') return false;
        length = length * 10 + static_cast<size_t>(ch - '0');
        if (length > max_body) return false;
    }
    if (length < request_header) return false;
    body.reserve(length);
    return reader([&](const char* data, size_t size) {
        if (size > length - body.size()) return false;
        body.append(data, size);
        return true;
    }) && body.size() == length;
}

} // namespace

bool register_http_routes(httplib::Server& server, bool enabled, const HttpAuthorize& authorized) {
    if (!enabled || !authorized) return false;

    server.Get(path, [authorized](const httplib::Request& req, httplib::Response& res) {
        if (!preflight(req, res, authorized)) return;
        res.set_content("{\"version\":" + std::to_string(version) +
                        ",\"maxBlocks\":" + std::to_string(max_blocks) +
                        ",\"maxBytes\":" + std::to_string(max_bytes) + "}", "application/json");
    });
    server.Post(path, [authorized](const httplib::Request& req, httplib::Response& res,
                                   const httplib::ContentReader& reader) {
        if (!preflight(req, res, authorized)) return;
        std::string body;
        Frame frame;
        if (req.has_header("X-AMDL-Playback") ||
            req.get_header_value_count("X-AMDL-Stream") != 1 || req.params.size() != 2 ||
            req.get_param_value_count("adamId") != 1 || req.get_param_value_count("uri") != 1 ||
            !read_body(req, reader, body) || !decode(body, frame)) {
            reject(res, 400, R"({"code":"native_batch_invalid"})");
            return;
        }
        auto result = decrypt(req.get_header_value("X-AMDL-Stream"), req.get_param_value("adamId"),
                              req.get_param_value("uri"), frame.first_block, frame.blocks.data(), frame.count,
                              [&] { return check_access(req, authorized); }, req.is_connection_closed);
        if (result.status == Status::Invalid) {
            reject(res, 400, R"({"code":"native_batch_invalid"})");
            return;
        }
        res.set_content(encode(frame, result), content_type);
    });
    enable();
    return true;
}

} // namespace native_batch
