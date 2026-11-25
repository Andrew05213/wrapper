// Loopback HTTP benchmark. Keep assertions enabled. No Android code or Apple service runs.
#include "native_fake_sdk.h"
#include "native_batch_wire.h"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <sys/wait.h>
#include <unistd.h>

namespace {
constexpr size_t total_blocks = 26372, play_every = 16;

struct Start {
    std::mutex mutex;
    std::condition_variable cv;
    size_t ready = 0;
    bool go = false;

    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        ++ready;
        cv.notify_all();
        cv.wait(lock, [&] { return go; });
    }

    NativeClock::time_point release(size_t workers) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return ready == workers; });
        const auto time = NativeClock::now();
        go = true;
        cv.notify_all();
        return time;
    }
};

struct Progress {
    std::mutex mutex;
    std::condition_variable cv;
    size_t finished = 0;

    void advance(size_t count) {
        std::lock_guard<std::mutex> lock(mutex);
        finished += count;
        cv.notify_one();
    }

    bool wait(size_t count) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return finished >= count; });
        return finished < total_blocks;
    }
};

uint64_t percentile(const std::vector<uint64_t>& sorted, size_t percent) {
    return sorted[(sorted.size() * percent + 99) / 100 - 1];
}

void measure(size_t round, bool batched, const std::vector<std::string>& inputs) {
    Logger::get().configure(LogLevel::Warn, "", 0);
    wire::Service service(batched, [](const httplib::Request&) { return true; });
    Start start;
    Progress progress;
    std::vector<std::thread> workers;
    std::array<size_t, 3> requests{};
    std::vector<uint64_t> play_ns;
    size_t play_started_during_download = 0;
    for (size_t n = 0; n < requests.size(); ++n) {
        workers.emplace_back([&, n] {
            httplib::Client client("127.0.0.1", service.port());
            wire::configure(client);
            const httplib::Headers headers = {{"X-AMDL-Stream", std::string(32, 'a' + n)}};
            const size_t first = total_blocks * n / requests.size();
            const size_t end = total_blocks * (n + 1) / requests.size();
            start.wait();
            for (size_t i = first; i < end; i += native_batch::max_blocks) {
                const auto count = std::min(native_batch::max_blocks, end - i);
                if (batched) {
                    auto body = wire::request(i - first, inputs.data() + i, count);
                    auto response = client.Post(wire::batch_path, headers, body, wire::type);
                    auto outputs = wire::reply(response, i - first, count, 0, count);
                    for (size_t j = 0; j < count; ++j) assert(outputs[j].size() == inputs[i + j].size());
                    ++requests[n];
                } else {
                    for (size_t j = 0; j < count; ++j) {
                        auto response = client.Post(wire::single_path, headers, inputs[i + j],
                                                    "application/octet-stream");
                        assert(response && response->status == 200 && response->body.size() == inputs[i + j].size());
                        ++requests[n];
                    }
                }
                // Publish identical milestones in both modes.
                progress.advance(count);
            }
        });
    }
    workers.emplace_back([&] {
        httplib::Client client("127.0.0.1", service.port());
        wire::configure(client);
        const httplib::Headers headers = {{"X-AMDL-Stream", std::string(32, 'd')}};
        play_ns.reserve(total_blocks / play_every);
        start.wait();
        for (size_t i = play_every; i <= total_blocks; i += play_every) {
            if (progress.wait(i)) ++play_started_during_download;
            auto before = NativeClock::now();
            auto response = client.Post(wire::single_path, headers, inputs[i - 1], "application/octet-stream");
            play_ns.push_back(native_ns(before, NativeClock::now()));
            assert(response && response->status == 200 && response->body.size() == inputs[i - 1].size());
        }
    });
    const auto before = start.release(workers.size());
    for (auto& thread : workers) thread.join();
    const auto wall = native_ns(before, NativeClock::now());
    std::sort(play_ns.begin(), play_ns.end());

    NativeReports reports;
    {
        PlaybackLock lock;
        native_streams.clear(reports);
    }
    NativeTotals reuse;
    uint64_t download_requests = 0, download_sdk_calls = 0;
    for (const auto& report : reports) {
        const auto blocks = report.first.sdk_calls + report.reuse.sdk_calls;
        if (blocks == play_ns.size()) continue;
        download_requests += report.transport_requests;
        download_sdk_calls += blocks;
        reuse.wait_ns += report.reuse.wait_ns;
        reuse.held_ns += report.reuse.held_ns;
        reuse.sdk_ns += report.reuse.sdk_ns;
        reuse.copy_ns += report.reuse.copy_ns;
    }
    assert(reports.size() == 4 && download_sdk_calls == total_blocks && live.empty());
    assert(download_requests == requests[0] + requests[1] + requests[2]);
    assert(calls == total_blocks + play_ns.size());

    size_t bytes = 0;
    for (const auto& input : inputs) bytes += input.size();
    printf("%zu,%s,%" PRIu64 ",%zu,%" PRIu64 ",%zu,%.3f,%.2f,%.3f,%.3f,%.3f,%zu,%.3f,%.3f,%.3f,%.3f\n",
           round, batched ? "batch" : "single", download_requests, play_ns.size(), download_sdk_calls, bytes,
           wall / 1e6, bytes * 1e9 / wall / (1024 * 1024), percentile(play_ns, 95) / 1e3,
           percentile(play_ns, 99) / 1e3, play_ns.back() / 1e3, play_started_during_download,
           reuse.wait_ns / 1e6, reuse.held_ns / 1e6, reuse.sdk_ns / 1e6, reuse.copy_ns / 1e6);
}

// Each trial starts from a fresh configuration, including the default-off
// admission queue. Fork only from this single-threaded process.
void trial(size_t round, bool batched, const std::vector<std::string>& inputs) {
    const auto child = fork();
    assert(child >= 0);
    if (child == 0) {
        measure(round, batched, inputs);
        fflush(stdout);
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
}

int main() {
    const size_t sizes[] = {4096, 8192, 12288, 13536};
    std::vector<std::string> inputs;
    inputs.reserve(total_blocks);
    for (size_t i = 0; i < total_blocks; ++i)
        inputs.emplace_back(sizes[i % 4], static_cast<char>(i));
    puts("round,mode,download_requests,play_requests,download_sdk_calls,download_bytes,wall_ms,download_MiB_s,play_p95_us,play_p99_us,play_max_us,play_started_during_download,reuse_wait_ms,reuse_held_ms,reuse_fake_sdk_ms,reuse_copy_ms");
    fflush(stdout);
    for (size_t round = 0; round <= 10; ++round) {
        trial(round, round % 2 != 0, inputs);
        trial(round, round % 2 == 0, inputs);
    }
}
