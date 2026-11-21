#pragma once

#include "logger.h"

constexpr unsigned kLoginTokenAttempts = 5;

enum class TokenStage {
    Storefront, DeveloperToken, MusicUserToken, MusicTokenFallback,
    StorefrontFile, DeveloperTokenFile, MusicTokenFile, JsonCache, Refresh
};
enum class TokenReason {
    StorefrontFetchFailed, DeveloperTokenFetchFailed, MusicUserTokenFetchFailed,
    CacheReadFailed, CacheInvalid, CacheWriteFailed, CacheReplaceFailed,
    CacheFallbackUsed, RetrySucceeded, RetryExhausted
};

inline const char* token_stage_name(TokenStage stage) {
    switch (stage) {
    case TokenStage::Storefront: return "storefront";
    case TokenStage::DeveloperToken: return "developer_token";
    case TokenStage::MusicUserToken: return "music_user_token";
    case TokenStage::MusicTokenFallback: return "music_token_fallback";
    case TokenStage::StorefrontFile: return "storefront_file";
    case TokenStage::DeveloperTokenFile: return "developer_token_file";
    case TokenStage::MusicTokenFile: return "music_token_file";
    case TokenStage::JsonCache: return "token_cache_json";
    case TokenStage::Refresh: return "refresh";
    }
    return nullptr;
}

inline const char* token_reason_name(TokenReason reason) {
    switch (reason) {
    case TokenReason::StorefrontFetchFailed: return "storefront_fetch_failed";
    case TokenReason::DeveloperTokenFetchFailed: return "developer_token_fetch_failed";
    case TokenReason::MusicUserTokenFetchFailed: return "music_user_token_fetch_failed";
    case TokenReason::CacheReadFailed: return "cache_read_failed";
    case TokenReason::CacheInvalid: return "cache_invalid";
    case TokenReason::CacheWriteFailed: return "cache_write_failed";
    case TokenReason::CacheReplaceFailed: return "cache_replace_failed";
    case TokenReason::CacheFallbackUsed: return "cache_fallback_used";
    case TokenReason::RetrySucceeded: return "retry_succeeded";
    case TokenReason::RetryExhausted: return "retry_exhausted";
    }
    return nullptr;
}

// Only the login path emits this independent stdout protocol. No input strings,
// filesystem paths, SDK messages or response bytes reach the formatter.
inline void report_token_diagnostic(TokenStage stage, TokenReason reason,
                                    unsigned attempt = 0, int http_status = 0,
                                    bool used_cache = false) {
    const char* phase = token_stage_name(stage);
    const char* code = token_reason_name(reason);
    if (!phase || !code || attempt > kLoginTokenAttempts) return;
    char http[32] = {};
    if (http_status >= 100 && http_status <= 599)
        snprintf(http, sizeof(http), ",\"httpStatus\":%d", http_status);
    char event[256];
    snprintf(event, sizeof(event),
             "WRAPPER_TOKEN_EVENT:{\"type\":\"login_token_diagnostic\",\"stage\":\"%s\","
             "\"reason\":\"%s\",\"attempt\":%u,\"usedCache\":%s%s}",
             phase, code, attempt, used_cache ? "true" : "false", http);
    puts(event);
    fflush(stdout);
    if (reason == TokenReason::CacheFallbackUsed || reason == TokenReason::RetrySucceeded)
        LOG_INFO("%s", event);
    else
        LOG_WARN("%s", event);
}
