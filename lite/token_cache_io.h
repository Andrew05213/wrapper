#pragma once

#include "token_diagnostics.h"
#include <memory>

enum class TokenFileResult { Ok, ReadFailed, Invalid, WriteFailed, ReplaceFailed };

inline TokenFileResult read_token_cache_file(const std::string& path, std::string& value) {
    value.clear();
    const auto close_file = [](FILE* file) { fclose(file); };
    std::unique_ptr<FILE, decltype(close_file)> file(fopen(path.c_str(), "rb"), close_file);
    if (!file) return TokenFileResult::ReadFailed;
    char buffer[4096];
    size_t length;
    while ((length = fread(buffer, 1, sizeof(buffer), file.get())) != 0)
        value.append(buffer, length);
    if (ferror(file.get())) {
        value.clear();
        return TokenFileResult::ReadFailed;
    }
    const auto last = value.find_last_not_of(" \n\r");
    if (last == std::string::npos || value.find('\0') != std::string::npos) {
        value.clear();
        return TokenFileResult::Invalid;
    }
    value.resize(last + 1);
    return TokenFileResult::Ok;
}

inline TokenFileResult replace_token_cache_file(const std::string& path, const char* data, size_t length) {
    const std::string tmp = path + ".tmp";
    FILE* file = fopen(tmp.c_str(), "wb");
    if (!file) return TokenFileResult::WriteFailed;
    const bool written = fwrite(data, 1, length, file) == length;
    const bool closed = fclose(file) == 0;
    if (!written || !closed) {
        remove(tmp.c_str());
        return TokenFileResult::WriteFailed;
    }
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        remove(tmp.c_str());
        return TokenFileResult::ReplaceFailed;
    }
    return TokenFileResult::Ok;
}

inline void report_token_file_failure(TokenStage stage, TokenFileResult result, unsigned attempt = 0) {
    switch (result) {
    case TokenFileResult::ReadFailed: report_token_diagnostic(stage, TokenReason::CacheReadFailed, attempt); break;
    case TokenFileResult::Invalid: report_token_diagnostic(stage, TokenReason::CacheInvalid, attempt); break;
    case TokenFileResult::WriteFailed: report_token_diagnostic(stage, TokenReason::CacheWriteFailed, attempt); break;
    case TokenFileResult::ReplaceFailed: report_token_diagnostic(stage, TokenReason::CacheReplaceFailed, attempt); break;
    case TokenFileResult::Ok: break;
    }
}
