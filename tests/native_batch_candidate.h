#pragma once
// Test-only experiment. The production single-block bridge is included by the fixture.
#include <atomic>

namespace candidate {
constexpr size_t max_blocks = 4, max_bytes = 64 * 1024;
enum class Status { Complete, Invalid, Cancelled, Failed };
struct Result {
    Status status;
    size_t attempted; // Includes a failing block. output contains the completed prefix only.
};

inline Result run(const std::string& stream, const std::string& adam, const std::string& uri,
                  const std::string* blocks, size_t count, std::vector<std::string>& output,
                  const std::atomic<bool>& cancelled) {
    output.clear();
    if (!blocks || !count || count > max_blocks || !valid_native_stream(stream, adam, uri))
        return {Status::Invalid, 0};
    size_t bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!valid_native_block(blocks[i]) || blocks[i].size() > max_bytes - bytes)
            return {Status::Invalid, 0};
        bytes += blocks[i].size();
    }
    output.reserve(count);
    StreamReporter reporter;
    auto entered = NativeClock::now();
    std::lock_guard<std::mutex> lock(g_playback_mutex);
    for (size_t i = 0; i < count; ++i) {
        if (cancelled.load()) return {Status::Cancelled, i};
        auto acquired = NativeClock::now();
        // One lock acquisition per batch; charge contention to its first block.
        if (i) entered = acquired;
        output.emplace_back();
        try {
            if (native_streams.decrypt(stream, adam, uri, blocks[i], output.back(),
                                       acquire_native_context, reporter.reports, entered,
                                       acquired, acquired)) continue;
        } catch (...) {
            // A throwing backend may have consumed the current block.
        }
        output.pop_back();
        return {Status::Failed, i + 1};
    }
    return {Status::Complete, count};
}
} // namespace candidate
