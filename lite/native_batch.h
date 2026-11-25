#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace native_batch {

constexpr uint8_t version = 1;
constexpr size_t max_blocks = 4;
constexpr size_t max_bytes = 64 * 1024;

// The caller owns the input until decrypt returns.
struct Block {
    const char* data;
    size_t size;
};

enum class Status : uint8_t {
    Complete = 0,
    Cancelled = 1,
    Conflict = 2,
    BlockFailed = 3,
    Unauthorized = 4,
    Busy = 5,
    Invalid = 6,
    Disabled = 7
};

struct Result {
    Status status = Status::Disabled;
    std::vector<std::string> output; // Completed prefix in input order
};

using Check = std::function<bool()>;

// Opt in during private service configuration, before any media workers start.
void enable();

// The authorization check runs before each turn, outside both locks.
// Cancellation is also checked after admission. Its probe must be nonblocking
// and must not reenter a media operation.
Result decrypt(const std::string& stream, const std::string& adam, const std::string& uri,
               uint64_t first_block, const Block* blocks, size_t count,
               const Check& authorized, const Check& cancelled = {});

} // namespace native_batch
