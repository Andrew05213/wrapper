#include "native_fake_sdk.h"
#include "native_batch_wire.h"
#include <array>
#include <unistd.h>

namespace {
const std::string secret = "FIXTURE-CAPABILITY";
const std::string stream(32, 'a');
const httplib::Headers headers = {{"X-AMDL-Stream", stream}, {"Authorization", secret}};
const std::array<std::string, 4> blocks = {
    std::string(16, 'q'), std::string(32, 'b'), std::string(16, '\0'), std::string(48, 'd')
};
std::atomic<bool> allowed{true};
std::atomic<unsigned> revoke_on{0};
std::atomic<unsigned> disconnect_on{0};
std::atomic<bool> processing{false}, disconnected{false};

bool authorize(const httplib::Request& req) {
    return req.get_header_value("Authorization") == secret && allowed;
}

void check_legacy() {
    wire::Service service(false, authorize);
    httplib::Client client("127.0.0.1", service.port());
    wire::configure(client);
    auto discovery = client.Get(wire::path, headers);
    assert(discovery && discovery->status == 404);
    auto no_batch = client.Post(wire::batch_path, headers, wire::request(0, blocks.data(), 4), wire::type);
    assert(no_batch && no_batch->status == 404 && calls == 0);
    auto response = client.Post(wire::single_path, headers, blocks[0], "application/octet-stream");
    assert(response && response->status == 200 && response->body.size() == 16);
    assert(response->body[0] == (blocks[0][0] ^ 1));
    assert(client.Delete("/native/decrypt", headers) && calls == 1);
}

void check_validation(httplib::Client& client) {
    auto discovery = client.Get(wire::path, headers);
    assert(discovery && discovery->status == 200);
    assert(discovery->body == R"({"version":1,"maxBlocks":4,"maxBytes":65536})");
    const auto before = calls.load();
    const auto frame = wire::request(0, blocks.data(), blocks.size());
    const httplib::Headers no_auth = {{"X-AMDL-Stream", stream}};
    auto unauth = client.Post(wire::batch_path, no_auth, frame, wire::type);
    assert(unauth && unauth->status == 401 && !unauth->has_header("EXCEPTION_WHAT"));
    assert(client.Get(wire::path, no_auth)->status == 401);
    auto play = headers;
    play.emplace("X-AMDL-Playback", "1");
    auto playback_batch = client.Post(wire::batch_path, play, frame, wire::type);
    assert(playback_batch && playback_batch->status == 400);

    auto reject = [&](const std::string& body, const std::string& path = wire::batch_path) {
        auto result = client.Post(path, headers, body, wire::type);
        assert(result && result->status == 400 && result->body == R"({"code":"native_batch_invalid"})");
    };
    std::string bad = frame;
    bad[0] = 2;
    reject(bad);
    bad = frame;
    bad[1] = 0;
    reject(bad);
    bad[1] = 5;
    reject(bad);
    reject(frame.substr(0, frame.size() - 1));
    reject(frame + "trailing");
    reject(frame, wire::batch_path + "&adamId=2");
    const std::string odd(17, 'p');
    reject(wire::request(0, &odd, 1));
    const std::string big((64 << 10) + 16, 'v');
    reject(wire::request(0, &big, 1));
    std::array<std::string, 2> oversize = {std::string(64 << 10, 'p'), std::string(16, 'u')};
    reject(wire::request(0, oversize.data(), oversize.size()));
    assert(calls == before);

    const auto at_limit = wire::request(0, oversize.data(), 1);
    auto result = client.Post(wire::batch_path, headers, at_limit, wire::type);
    assert(wire::reply(result, 0, 1, 0, 1)[0].size() == 65536);
    assert(client.Delete("/native/decrypt", headers));
    auto single = client.Post(wire::single_path, headers, big, "application/octet-stream");
    assert(single && single->status == 200 && single->body.size() == big.size());
    assert(client.Delete("/native/decrypt", headers));
}

// Send the real framed request and close before receiving any response. The
// fake backend holds its specified block until the client closes the socket.
void drop_reply(int port, unsigned block, const std::string& frame) {
    processing = false;
    disconnected = false;
    disconnect_on = block;
    int socket = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(socket >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    std::string request = "POST " + wire::batch_path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                          "Content-Type: " + wire::type + "\r\nContent-Length: " +
                          std::to_string(frame.size()) + "\r\nX-AMDL-Stream: " + stream +
                          "\r\nAuthorization: " + secret + "\r\n\r\n" + frame;
    size_t offset = 0;
    while (offset < request.size()) {
        auto sent = ::send(socket, request.data() + offset, request.size() - offset, 0);
        assert(sent > 0);
        offset += sent;
    }
    const auto deadline = NativeClock::now() + std::chrono::seconds(5);
    while (!processing) {
        assert(NativeClock::now() < deadline);
        std::this_thread::yield();
    }
    assert(::shutdown(socket, SHUT_RDWR) == 0);
    assert(::close(socket) == 0);
    disconnected = true;
}

void check_outcomes() {
    before_sample = [](char, unsigned sequence) {
        if (sequence == revoke_on) allowed = false;
        if (sequence == disconnect_on) {
            processing = true;
            const auto deadline = NativeClock::now() + std::chrono::seconds(5);
            while (!disconnected) {
                assert(NativeClock::now() < deadline);
                std::this_thread::yield();
            }
        }
    };
    wire::Service service(true, authorize);
    httplib::Client client("127.0.0.1", service.port());
    wire::configure(client);
    check_validation(client);

    std::vector<std::string> expected;
    for (const auto& block : blocks) {
        auto response = client.Post(wire::single_path, headers, block, "application/octet-stream");
        assert(response && response->status == 200);
        expected.push_back(response->body);
    }
    assert(client.Delete("/native/decrypt", headers));
    const auto frame = wire::request(0, blocks.data(), blocks.size());
    auto send = [&] { return client.Post(wire::batch_path, headers, frame, wire::type); };
    const std::vector<std::string> prefix(expected.begin(), expected.begin() + 2);

    auto before = calls.load();
    auto complete = send();
    assert(wire::reply(complete, 0, 4, 0, 4) == expected);
    assert(calls == before + 4);
    assert(client.Delete("/native/decrypt", headers));

    before = calls;
    fail_on_block = 3;
    auto failed = send();
    assert(wire::reply(failed, 0, 4, 3, 2) == prefix);
    assert(calls == before + 3 && !failed->has_header("EXCEPTION_WHAT"));
    fail_on_block = 0;

    before = calls;
    revoke_on = 2;
    auto denied = send();
    assert(wire::reply(denied, 0, 4, 4, 2) == prefix);
    assert(calls == before + 2);
    allowed = true;
    revoke_on = 0;
    assert(client.Delete("/native/decrypt", headers));

    // Both known test cut points are unknown at the client. The old ordinal
    // is rejected while the stream is live, whether a prefix or all ran.
    for (unsigned block : {1, 4}) {
        before = calls;
        drop_reply(service.port(), block, frame);
        auto duplicate = send(); // Deliberately stale request to exercise the server guard.
        assert(wire::reply(duplicate, 0, 4, 2, 0).empty());
        assert(client.Delete("/native/decrypt", headers));
        assert(calls == before + block);
        disconnect_on = 0;
    }
}
}

int main() {
    Logger::get().configure(LogLevel::Warn, "", 0);
    httplib::Server unconfigured;
    assert(!native_batch::register_http_routes(unconfigured, true, {}));
    check_legacy();
    check_outcomes();
    native_decrypt_close_all();
    assert(live.empty() && cache.empty());
    puts("native batch HTTP: opt-in, capability guard, framing limits, ordered outputs, prefix failures and lost replies passed");
}
