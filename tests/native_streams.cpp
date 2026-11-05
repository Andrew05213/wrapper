#include "../lite/native_streams.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>

static int live = 0, created = 0;
static bool fail_decrypt = false, throw_decrypt = false;
struct Context final : NativeDecryptContext {
    Context() { ++live; ++created; }
    ~Context() { --live; }
    bool decrypt(const std::string& input, std::string& output) override {
        if (throw_decrypt) throw std::runtime_error("decrypt");
        if (fail_decrypt) return false;
        output = input;
        return true;
    }
};

int main() {
    using Clock = NativeStreams::Clock;
    auto time = Clock::time_point(std::chrono::seconds(1000));
    NativeStreams streams;
    auto factory = [](const std::string&, const std::string&) {
        return std::unique_ptr<NativeDecryptContext>(new Context);
    };
    const std::string sample(16, 'x');
    std::string output;
    // Thousands of tracks must not accumulate FairPlay owners.
    for (int i = 0; i < 2048; ++i) {
        assert(streams.decrypt("a", std::to_string(i), "key", sample, output, factory, time));
        assert(output == sample && live == 1);
        streams.close("a");
        streams.close("a");
        assert(live == 0);
    }
    assert(streams.decrypt("a", "1", "key", sample, output, factory, time));
    assert(streams.decrypt("b", "2", "key", sample, output, factory, time));
    int before = created;
    assert(streams.decrypt("a", "1", "key", sample, output, factory, time));
    assert(created == before && live == 2);
    // Changing a key drops only that stream's old owner.
    assert(streams.decrypt("a", "1", "other", sample, output, factory, time));
    assert(created == before + 1 && live == 2);
    streams.close("a");
    assert(live == 1);
    assert(streams.decrypt("b", "2", "key", sample, output, factory, time));
    streams.clear();
    assert(live == 0);

    // Simulate a crashed client: full capacity recovers without a DELETE.
    for (int i = 0; i < 32; ++i)
        assert(streams.decrypt(std::to_string(i), "1", "key", sample, output, factory, time));
    assert(!streams.decrypt("full", "1", "key", sample, output, factory, time));
    assert(output.empty() && live == 32);
    auto later = time + std::chrono::seconds(119);
    assert(streams.decrypt("0", "1", "key", sample, output, factory, later));
    streams.expire(time + std::chrono::seconds(120));
    assert(live == 1); // The active stream survives stale-stream collection.
    assert(streams.decrypt("new", "2", "key", sample, output, factory, later));
    assert(live == 2);
    streams.clear();

    fail_decrypt = true;
    assert(!streams.decrypt("bad", "1", "key", sample, output, factory, time));
    assert(output.empty() && live == 0);
    fail_decrypt = false;
    throw_decrypt = true;
    try {
        streams.decrypt("bad", "1", "key", sample, output, factory, time);
        assert(false);
    } catch (const std::runtime_error&) {}
    assert(output.empty() && live == 0);
    throw_decrypt = false;
    auto missing = [](const std::string&, const std::string&) {
        return std::unique_ptr<NativeDecryptContext>();
    };
    assert(!streams.decrypt("missing", "1", "key", sample, output, missing, time));
    assert(live == 0);
    puts("native streams: lifetime, capacity, idle recovery and failure cleanup passed");
}
