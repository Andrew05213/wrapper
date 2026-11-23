#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

using NativeClock = std::chrono::steady_clock;
inline uint64_t native_ns(NativeClock::time_point from, NativeClock::time_point to) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(to - from).count();
}

// Timings include time spent unwinding a failed SDK call.
struct NativeTimer {
    explicit NativeTimer(uint64_t& total) : total(total), start(NativeClock::now()) {}
    ~NativeTimer() { total += native_ns(start, NativeClock::now()); }
    uint64_t& total;
    NativeClock::time_point start;
};

struct NativeTotals {
    uint64_t requests = 0, bytes = 0, completed_bytes = 0;
    uint64_t wait_ns = 0, create_ns = 0, copy_ns = 0, sdk_ns = 0, held_ns = 0;
    double bytes_per_second() const {
        const auto elapsed = wait_ns + held_ns;
        return elapsed ? 1e9 * completed_bytes / elapsed : 0;
    }
};

enum class NativeStreamEnd { Close, Idle, Failure, Shutdown };
struct NativeSummary {
    NativeTotals first, reuse;
    NativeStreamEnd end = NativeStreamEnd::Close;
};
using NativeReports = std::vector<NativeSummary>;

// Access and SDK object destruction are serialized by g_playback_mutex.
// Completed summaries contain only counters and are emitted after unlocking.
class NativeDecryptContext {
public:
    virtual ~NativeDecryptContext() = default;
    virtual bool decrypt(const std::string& input, std::string& output, NativeTotals& times) = 0;
};

class NativeStreams {
public:
    using Clock = NativeClock;
    using Factory = std::function<std::unique_ptr<NativeDecryptContext>(
        const std::string&, const std::string&)>;

    explicit NativeStreams(std::chrono::seconds idle = std::chrono::seconds(120))
        : idle_(idle) {}

    bool decrypt(const std::string& stream, const std::string& adam,
                 const std::string& uri, const std::string& input,
                 std::string& output, const Factory& factory, NativeReports& reports,
                 Clock::time_point entered, Clock::time_point acquired,
                 Clock::time_point now) {
        output.clear();
        expire(reports, now);
        auto found = streams_.find(stream);
        if (found == streams_.end()) {
            if (streams_.size() >= 32) return false;
            found = streams_.emplace(stream, Entry{}).first;
        }
        auto& entry = found->second;
        const bool first = !entry.context || entry.adam != adam || entry.uri != uri;
        auto& times = first ? entry.summary.first : entry.summary.reuse;
        ++times.requests;
        times.bytes += input.size();
        times.wait_ns += native_ns(entered, acquired);
        bool ok = false;
        try {
            if (first) {
                entry.context.reset();
                entry.adam = adam;
                entry.uri = uri;
                NativeTimer timer(times.create_ns);
                entry.context = factory(adam, uri);
            }
            ok = entry.context && entry.context->decrypt(input, output, times);
        } catch (...) {
            output.clear();
            finish_failure(found, first, acquired, reports);
            throw;
        }
        if (!ok) {
            output.clear();
            finish_failure(found, first, acquired, reports);
            return false;
        }
        entry.used = now;
        times.completed_bytes += input.size();
        times.held_ns += native_ns(acquired, Clock::now());
        return true;
    }

    void close(const std::string& stream, NativeReports& reports) {
        auto found = streams_.find(stream);
        if (found != streams_.end()) retire(found, NativeStreamEnd::Close, reports);
    }
    void clear(NativeReports& reports) {
        while (!streams_.empty()) retire(streams_.begin(), NativeStreamEnd::Shutdown, reports);
    }
    void expire(NativeReports& reports, Clock::time_point now = Clock::now()) {
        for (auto it = streams_.begin(); it != streams_.end();) {
            if (now - it->second.used >= idle_) retire(it++, NativeStreamEnd::Idle, reports);
            else ++it;
        }
    }

private:
    struct Entry {
        std::string adam, uri;
        std::unique_ptr<NativeDecryptContext> context;
        Clock::time_point used;
        NativeSummary summary;
    };
    using Entries = std::map<std::string, Entry>;
    Entries streams_;
    std::chrono::seconds idle_;

    void retire(Entries::iterator it, NativeStreamEnd end, NativeReports& reports) {
        it->second.summary.end = end;
        auto summary = it->second.summary;
        streams_.erase(it);
        reports.push_back(summary);
    }
    void finish_failure(Entries::iterator it, bool first, Clock::time_point acquired,
                        NativeReports& reports) {
        retire(it, NativeStreamEnd::Failure, reports);
        auto& times = first ? reports.back().first : reports.back().reuse;
        times.held_ns += native_ns(acquired, Clock::now());
    }
};
