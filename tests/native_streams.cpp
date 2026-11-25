#include "../lite/native_streams.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>

static int live = 0, created = 0;
static bool fail_decrypt = false, throw_decrypt = false;
struct Context final : NativeDecryptContext {
    Context() { ++live; ++created; }
    ~Context() { --live; }
    bool decrypt(native_batch::Block input, std::string& output, NativeTotals& times) override {
        ++times.sdk_calls;
        if (throw_decrypt) throw std::runtime_error("decrypt");
        if (fail_decrypt) return false;
        output.assign(input.data, input.size);
        return true;
    }
};

int main() {
    using Clock = NativeStreams::Clock;
    auto time = Clock::time_point(std::chrono::seconds(1000));
    NativeStreams streams;
    NativeReports reports;
    auto acquired = Clock::now();
    auto entered = acquired - std::chrono::nanoseconds(40);
    auto factory = [](const std::string&, const std::string&) {
        return std::unique_ptr<NativeDecryptContext>(new Context);
    };
    const std::string sample(16, 'x');
    const native_batch::Block block{sample.data(), sample.size()};
    std::string output;
    auto decrypt = [&](const std::string& stream, const std::string& adam, const std::string& uri) {
        return streams.decrypt(stream, adam, uri, block, output, factory, reports, entered, acquired, time)
            == native_batch::Status::Complete;
    };
    // Thousands of tracks must not accumulate FairPlay owners.
    for (int i = 0; i < 2048; ++i) {
        assert(decrypt("a", std::to_string(i), "key"));
        assert(output == sample && live == 1);
        streams.close("a", reports);
        streams.close("a", reports);
        assert(live == 0);
    }
    assert(decrypt("a", "1", "key"));
    assert(decrypt("b", "2", "key"));
    int before = created;
    assert(decrypt("a", "1", "key"));
    assert(created == before && live == 2);
    assert(reports.size() == 2048);
    reports.clear();
    // Changing a key drops only that stream's old owner.
    assert(decrypt("a", "1", "other"));
    assert(created == before + 1 && live == 2);
    streams.close("a", reports);
    assert(live == 1);
    assert(reports.size() == 1 && reports[0].end == NativeStreamEnd::Close);
    assert(reports[0].first.requests == 2 && reports[0].reuse.requests == 1);
    assert(reports[0].first.wait_ns == 80 && reports[0].reuse.wait_ns == 40);
    assert(reports[0].transport_requests == 3 && reports[0].first.sdk_calls == 2 &&
           reports[0].reuse.sdk_calls == 1);
    assert(reports[0].first.completed_bytes == 32 && reports[0].reuse.bytes == 16);
    assert(decrypt("b", "2", "key"));
    streams.clear(reports);
    assert(live == 0);

    // Simulate a crashed client: full capacity recovers without a DELETE.
    for (int i = 0; i < 32; ++i)
        assert(decrypt(std::to_string(i), "1", "key"));
    assert(!decrypt("full", "1", "key"));
    assert(output.empty() && live == 32);
    auto expire_at = time + std::chrono::seconds(120);
    time += std::chrono::seconds(119);
    assert(decrypt("0", "1", "key"));
    streams.expire(reports, expire_at);
    assert(reports.back().end == NativeStreamEnd::Idle);
    assert(live == 1); // The active stream survives stale-stream collection.
    assert(decrypt("new", "2", "key"));
    assert(live == 2);
    streams.clear(reports);

    fail_decrypt = true;
    assert(!decrypt("bad", "1", "key"));
    assert(output.empty() && live == 0);
    fail_decrypt = false;
    throw_decrypt = true;
    try {
        decrypt("bad", "1", "key");
        assert(false);
    } catch (const std::runtime_error&) {}
    assert(output.empty() && live == 0);
    throw_decrypt = false;
    auto missing = [](const std::string&, const std::string&) {
        return std::unique_ptr<NativeDecryptContext>();
    };
    assert(streams.decrypt("missing", "1", "key", block, output, missing, reports, entered, acquired, time)
           == native_batch::Status::BlockFailed);
    assert(live == 0);
    assert(reports.back().end == NativeStreamEnd::Failure);
    assert(reports.back().first.completed_bytes == 0 && reports.back().first.requests == 1 &&
           reports.back().first.sdk_calls == 0);
    puts("native streams: lifetime, capacity, idle recovery and failure cleanup passed");
}
