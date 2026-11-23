// Build in release mode (keep assertions enabled). No transport or Android code is executed.
#include "native_fake_sdk.h"
#include "native_batch_candidate.h"
#include <algorithm>
#include <condition_variable>
#include <thread>

namespace {
const size_t total_blocks = 8188, play_every = 16;
const std::string uri = "skd://itunes.apple.com/synthetic";
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
};

// Identical deterministic inputs, stream-local order and output lifetimes in both modes.
// Pace playback by download progress so samples cover the contended interval.
void measure(size_t round, size_t width, const std::vector<std::string>& inputs) {
    Start start;
    std::atomic<size_t> progress{0};
    std::vector<std::thread> workers;
    std::vector<uint64_t> play_ns;
    size_t contended_samples = 0;
    for (size_t n = 0; n < 3; ++n) {
        workers.emplace_back([&, n] {
            std::string stream(32, 'a'+n);
            size_t first = total_blocks*n/3, end = total_blocks*(n+1)/3;
            start.wait();
            for (size_t i = first; i < end; i += candidate::max_blocks) {
                const auto count = std::min(candidate::max_blocks, end-i);
                if (width == 1) {
                    for (size_t j = 0; j < count; ++j) {
                        std::string out;
                        assert(native_decrypt(stream, "1", uri, inputs[i+j], out));
                    }
                } else {
                    std::vector<std::string> out;
                    assert(candidate::run(stream, "1", uri, &inputs[i], count, out, cancelled).status
                           == candidate::Status::Complete);
                }
                progress.fetch_add(count, std::memory_order_relaxed);
            }
        });
    }
    workers.emplace_back([&] {
        const std::string stream(32, 'd');
        play_ns.reserve(total_blocks/play_every);
        start.wait();
        for (size_t i = play_every; i <= total_blocks; i += play_every) {
            while (progress.load(std::memory_order_relaxed) < i) std::this_thread::yield();
            std::string out;
            if (progress.load(std::memory_order_relaxed) < total_blocks) ++contended_samples;
            auto before = NativeClock::now();
            assert(native_decrypt(stream, "2", uri, inputs[i-1], out));
            play_ns.push_back(native_ns(before, NativeClock::now()));
        }
    });
    std::unique_lock<std::mutex> lock(start.mutex);
    start.cv.wait(lock, [&] { return start.ready == workers.size(); });
    const auto before = NativeClock::now();
    start.go = true;
    lock.unlock();
    start.cv.notify_all();
    for (auto& worker : workers) worker.join();
    const auto wall = native_ns(before, NativeClock::now());
    std::sort(play_ns.begin(), play_ns.end());
    NativeReports reports;
    native_streams.clear(reports); // workers joined, collect counters without timing log I/O
    NativeTotals download;
    uint64_t create_ns = 0;
    for (const auto& report : reports) {
        if (report.reuse.requests + 1 == play_ns.size()) continue;
        create_ns += report.first.create_ns;
        download.requests += report.reuse.requests;
        download.wait_ns += report.reuse.wait_ns;
        download.copy_ns += report.reuse.copy_ns;
        download.sdk_ns += report.reuse.sdk_ns;
        download.held_ns += report.reuse.held_ns;
    }
    assert(reports.size() == 4 && download.requests == total_blocks - 3 && live.empty());
    size_t bytes = 0;
    for (const auto& input : inputs) bytes += input.size();
    printf("%zu,%zu,%.3f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%zu,%.3f\n", round, width, wall/1e6,
           bytes * 1e9 / wall / (1024*1024), play_ns[(play_ns.size()*95+99)/100-1]/1e3,
           download.wait_ns/1e6, download.copy_ns/1e6, download.sdk_ns/1e6, download.held_ns/1e6, play_ns.back()/1e3, contended_samples, create_ns/1e3);
}
}

int main() {
    std::vector<std::string> inputs;
    for (size_t i = 0; i < total_blocks; ++i)
        inputs.emplace_back((1 + (i % 4)) * 4096, static_cast<char>(i));
    puts("round,blocks,wall_ms,download_MiB_s,play_p95_us,reuse_wait_ms,reuse_copy_ms,reuse_fake_sdk_ms,reuse_held_ms,play_max_us,play_samples_started_during_download,first_create_us");
    // Round zero is warm-up. Reverse order every pair.
    for (size_t round = 0; round <= 10; ++round) {
        measure(round, round % 2 ? 4 : 1, inputs);
        measure(round, round % 2 ? 1 : 4, inputs);
    }
}
