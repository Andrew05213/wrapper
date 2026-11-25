#include "native_fake_sdk.h"
#include <array>
#include <thread>
#include <unistd.h>

namespace {
using native_batch::Block;
using native_batch::Status;
const std::string a(32, 'a'), b(32, 'b'), c(32, 'c');
const std::string adam = "987654321", uri = "skd://itunes.apple.com/private-uri-sentinel";
const std::array<std::string, 4> samples = {
    "AUDIO-SENTINEL!!", std::string(32, 'b'), std::string(16, '\0'), std::string(48, 'd')
};
const std::array<Block, 4> blocks = {{
    {samples[0].data(), samples[0].size()}, {samples[1].data(), samples[1].size()},
    {samples[2].data(), samples[2].size()}, {samples[3].data(), samples[3].size()}
}};
const native_batch::Check allow = [] { return true; };

native_batch::Result batch(const std::string& stream, uint64_t first = 0, size_t count = 4) {
    return native_batch::decrypt(stream, adam, uri, first, blocks.data() + first, count, allow);
}

template<class Predicate>
void wait_for(Predicate ready) {
    const auto deadline = NativeClock::now() + std::chrono::seconds(5);
    while (!ready()) {
        assert(NativeClock::now() < deadline);
        std::this_thread::yield();
    }
}

void check_summary_privacy() {
    FILE* capture = tmpfile();
    assert(capture);
    fflush(stderr);
    int saved = dup(STDERR_FILENO);
    assert(saved >= 0 && dup2(fileno(capture), STDERR_FILENO) >= 0);

    std::string output;
    assert(native_decrypt(a, adam, uri, samples[0], output));
    assert(batch(a, 1, 3).status == Status::Complete);
    fflush(stderr);
    assert(ftell(capture) == 0);
    native_decrypt_close(a);
    fail_on_block = 3;
    assert(batch(b).status == Status::BlockFailed);
    fail_on_block = 0;
    fflush(stderr);
    assert(dup2(saved, STDERR_FILENO) >= 0);
    close(saved);

    rewind(capture);
    char line[2048];
    assert(fgets(line, sizeof(line), capture));
    const std::string closed(line);
    assert(fgets(line, sizeof(line), capture));
    const std::string failed(line);
    assert(!fgets(line, sizeof(line), capture));
    fclose(capture);
    assert(closed.find("native_stream_summary end=close transport_requests=2 first={requests=1") != std::string::npos);
    assert(closed.find("bytes=16 completed_bytes=16 sdk_calls=1") != std::string::npos);
    assert(closed.find("reuse={requests=3 bytes=96 completed_bytes=96 sdk_calls=3") != std::string::npos);
    assert(failed.find("native_stream_summary end=failure transport_requests=1") != std::string::npos);
    assert(failed.find("reuse={requests=2 bytes=48 completed_bytes=32 sdk_calls=2") != std::string::npos);
    for (const auto& sentinel : {a, b, adam, uri, samples[0], std::string("persistent-key"),
                                 std::string("SDK-SENSITIVE-EXCEPTION")}) {
        assert(closed.find(sentinel) == std::string::npos);
        assert(failed.find(sentinel) == std::string::npos);
    }
    assert(live.empty());
}

std::vector<std::string> check_streams() {
    std::vector<std::string> expected;
    for (const auto& input : samples) {
        expected.emplace_back();
        assert(native_decrypt(a, adam, uri, input, expected.back()));
    }
    native_decrypt_close(a);
    for (size_t offset : {0, 2}) {
        for (const auto& stream : {a, b, c}) {
            const auto result = batch(stream, offset, 2);
            assert(result.status == Status::Complete);
            assert(result.output == std::vector<std::string>(expected.begin() + offset, expected.begin() + offset + 2));
        }
    }
    assert(batch(a, 0, 1).status == Status::Conflict);
    assert(native_batch::decrypt(a, adam, uri, 5, blocks.data(), 1, allow).status == Status::Conflict);
    const auto new_uri = uri + "/context2";
    assert(native_batch::decrypt(a, adam, new_uri, 4, blocks.data(), 1, allow).status == Status::Conflict);
    std::string out;
    assert(native_decrypt(a, adam, new_uri, samples[0], out) && out == expected[0]);
    auto continued = native_batch::decrypt(a, adam, new_uri, 5, blocks.data() + 1, 3, allow);
    assert(continued.status == Status::Complete);
    assert(continued.output == std::vector<std::string>(expected.begin() + 1, expected.end()));

    // A valid legacy block larger than the batch bound consumes one ordinal.
    const std::string large(1 << 20, 'x');
    const Block big{large.data(), large.size()};
    assert(native_batch::decrypt(b, adam, uri, 4, &big, 1, allow).status == Status::Invalid);
    assert(native_decrypt(b, adam, uri, large, out) && out.size() == large.size());
    assert(native_batch::decrypt(b, adam, uri, 5, blocks.data(), 1, allow).status == Status::Complete);
    native_decrypt_close_all();
    assert(live.empty());
    return expected;
}

void check_failures(const std::vector<std::string>& expected) {
    const auto before = calls.load();
    const Block bad{nullptr, 16};
    for (size_t count : {0, 5}) assert(batch(a, 0, count).status == Status::Invalid);
    assert(native_batch::decrypt(a, adam, uri, 0, &bad, 1, allow).status == Status::Invalid);
    assert(native_batch::decrypt(a, adam, uri, UINT64_MAX, blocks.data(), 1, allow).status == Status::Invalid);
    assert(native_batch::decrypt(a, adam, uri, 0, blocks.data(), 4, {}).status == Status::Unauthorized);
    auto decrypt = [&] {
        return native_batch::decrypt(a, adam, uri, 0, blocks.data(), 4, allow, [] { return cancelled.load(); });
    };
    cancelled = true;
    auto result = decrypt();
    assert(result.status == Status::Cancelled && result.output.empty());
    assert(calls == before && live.empty());

    cancelled = false;
    before_sample = [](char, unsigned sequence) { if (sequence == 2) cancelled = true; };
    result = decrypt();
    assert(result.status == Status::Cancelled);
    assert(result.output == std::vector<std::string>(expected.begin(), expected.begin() + 2));
    assert(calls == before + 2);
    native_decrypt_close(a);
    before_sample = {};
    cancelled = false;

    fail_on_block = 3;
    result = batch(a);
    assert(result.status == Status::BlockFailed);
    assert(result.output == std::vector<std::string>(expected.begin(), expected.begin() + 2));
    assert(calls == before + 5 && live.empty());
    fail_on_block = 0;
    fail_context = true;
    result = batch(a);
    assert(result.status == Status::BlockFailed && result.output.empty());
    assert(calls == before + 5 && live.empty());
    fail_context = false;
}

void check_close_between_blocks() {
    size_t checks = 0;
    const auto replacement_uri = uri + "/replacement";
    auto result = native_batch::decrypt(a, adam, uri, 0, blocks.data(), 4, [&] {
        if (++checks != 2) return true;
        std::string out;
        assert(!native_decrypt(a, adam, uri, samples[1], out));
        assert(batch(a, 1, 1).status == Status::Conflict);
        native_decrypt_close(a);
        assert(native_decrypt(a, adam, replacement_uri, samples[0], out));
        return true;
    });
    assert(result.status == Status::Cancelled && result.output.size() == 1);
    assert(native_batch::decrypt(a, adam, replacement_uri, 1, blocks.data(), 1, allow).status == Status::Complete);
    native_decrypt_close(a);
}

void check_stream_limit() {
    for (unsigned i = 0; i < 32; ++i) {
        char id[33];
        snprintf(id, sizeof(id), "%032x", i);
        std::string out;
        assert(native_decrypt(id, adam, uri, samples[0], out));
    }
    const auto before = calls.load();
    auto full = batch(a);
    assert(full.status == Status::Busy && full.output.empty() && calls == before);
    native_decrypt_close_all();
    assert(live.empty());
}

void check_fair_turns() {
    std::atomic<bool> first_active{false}, release_first{false};
    std::vector<char> order;
    before_sample = [&](char kind, unsigned sequence) {
        order.push_back(kind);
        if (kind == 'p' && sequence == 1) {
            first_active = true;
            wait_for([&] { return release_first.load(); });
        }
    };
    const std::string play(16, 'p'), download(16, 'w');
    const Block work[] = {{download.data(), download.size()}, {download.data(), download.size()}};
    auto play_block = [&] {
        std::string out;
        assert(native_decrypt(b, adam, uri, play, out));
    };
    std::thread active(play_block);
    wait_for([&] { return first_active.load(); });

    // Cancellation while queued survives admission without starting SDK work.
    native_batch::Result stopped;
    std::thread cancelled_waiter([&] {
        stopped = native_batch::decrypt(c, adam, uri, 0, work, 2, allow, [] { return cancelled.load(); });
    });
    wait_for([] { return playback_admission.waiting() == 1; });
    cancelled = true;

    std::thread downloading([&] {
        assert(native_batch::decrypt(a, adam, uri, 0, work, 2, allow).status == Status::Complete);
    });
    wait_for([] { return playback_admission.waiting() == 2; });
    std::thread playing(play_block);
    wait_for([] { return playback_admission.waiting() == 3; });
    release_first = true;
    active.join();
    cancelled_waiter.join();
    downloading.join();
    playing.join();
    assert(stopped.status == Status::Cancelled && stopped.output.empty());
    // The waiting download gets its first turn; already waiting playback gets
    // the next turn before the second block of that same download request.
    assert((order == std::vector<char>{'p', 'w', 'p', 'w'}));
    before_sample = {};
    cancelled = false;
    native_decrypt_close_all();
    assert(live.empty());
}
}

int main() {
    assert(batch(a).status == Status::Disabled && calls == 0);
    native_batch::enable();
    check_summary_privacy();
    Logger::get().configure(LogLevel::Warn, "", 0);
    auto expected = check_streams();
    check_failures(expected);
    check_close_between_blocks();
    check_stream_limit();
    check_fair_turns();
    puts("native per-block turns, stream isolation, counts, privacy, cancellation and failures passed");
}
