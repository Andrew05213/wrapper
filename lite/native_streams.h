#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>

// The caller serializes access with g_playback_mutex, including expiration.
// Contexts own their Android shared_ptr; no controller-cache pointer is borrowed.
class NativeDecryptContext {
public:
    virtual ~NativeDecryptContext() = default;
    virtual bool decrypt(const std::string& input, std::string& output) = 0;
};

class NativeStreams {
public:
    using Clock = std::chrono::steady_clock;
    using Factory = std::function<std::unique_ptr<NativeDecryptContext>(
        const std::string&, const std::string&)>;

    explicit NativeStreams(std::chrono::seconds idle = std::chrono::seconds(120))
        : idle_(idle) {}

    bool decrypt(const std::string& stream, const std::string& adam,
                 const std::string& uri, const std::string& input,
                 std::string& output, const Factory& factory,
                 Clock::time_point now = Clock::now()) {
        output.clear();
        expire(now);
        auto found = streams_.find(stream);
        if (found != streams_.end() &&
            (found->second.adam != adam || found->second.uri != uri)) {
            streams_.erase(found);
            found = streams_.end();
        }
        if (found == streams_.end()) {
            if (streams_.size() >= 32) return false;
            auto context = factory(adam, uri);
            if (!context) return false;
            Entry entry{adam, uri, std::move(context), now};
            found = streams_.emplace(stream, std::move(entry)).first;
        }
        try {
            if (!found->second.context->decrypt(input, output)) {
                streams_.erase(found);
                output.clear();
                return false;
            }
        } catch (...) {
            streams_.erase(found);
            output.clear();
            throw;
        }
        found->second.used = now;
        return true;
    }

    void close(const std::string& stream) { streams_.erase(stream); }
    void clear() { streams_.clear(); }
    void expire(Clock::time_point now = Clock::now()) {
        for (auto it = streams_.begin(); it != streams_.end();) {
            if (now - it->second.used >= idle_) it = streams_.erase(it);
            else ++it;
        }
    }

private:
    struct Entry {
        std::string adam, uri;
        std::unique_ptr<NativeDecryptContext> context;
        Clock::time_point used;
    };
    std::map<std::string, Entry> streams_;
    std::chrono::seconds idle_;
};
