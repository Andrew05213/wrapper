#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace AuthInteractionLimits {
constexpr uint32_t Version = 1;
constexpr size_t Title = 1024, Message = 8192, Label = 512;
constexpr size_t Account = 512, Input = 4096, Code = 6;
constexpr size_t Buttons = 16, Fields = 16, DisplayTotal = 24576, InputTotal = 16384;
constexpr uint32_t DefaultTimeoutMs = 120000, MaxTimeoutMs = 300000, MinTimeoutMs = 10;
constexpr size_t Workers = 8;
}

enum class AuthInteractionKind : uint32_t {
    AccountChoice = 1, AuthenticationFailure = 2, ProtocolDialog = 3,
    Credentials = 4, VerificationCode = 5, Unsupported = 6
};
enum class AuthInteractionFieldType : uint32_t {
    Text = 1, Secret = 2, Email = 3, Digits = 4, VerificationCode = 5, Unsupported = 6, Account = 7
};
enum class AuthInteractionButtonRole : uint32_t { Choice = 1, Submit = 2, Cancel = 3, Retry = 4 };
enum class AuthInteractionUnsupported : uint32_t {
    None = 0, FieldType = 1, Action = 2, Metadata = 3, Limits = 4, MissingCredentials = 5
};
enum class AuthInteractionDisposition : uint32_t { Submit = 1, Cancel = 2, Unsupported = 3 };
enum class AuthInteractionSubmitResult : uint32_t {
    Accepted = 0, NoPendingRequest = 1, WrongRequest = 2, InvalidResponse = 3,
    AlreadyAnswered = 4, Expired = 5
};

inline void auth_interaction_wipe(std::string& text) {
    volatile char* p = text.empty() ? nullptr : &text[0];
    for (size_t i = 0; i < text.size(); ++i) p[i] = 0;
    text.clear();
}

struct AuthInteractionButton {
    uint64_t id = 0;
    std::string label;
    AuthInteractionButtonRole role = AuthInteractionButtonRole::Choice;
    bool enabled = true;
    AuthInteractionUnsupported unsupported = AuthInteractionUnsupported::None;
    ~AuthInteractionButton() { auth_interaction_wipe(label); }
};
struct AuthInteractionField {
    uint64_t id = 0;
    AuthInteractionFieldType type = AuthInteractionFieldType::Unsupported;
    std::string label;
    bool required = true;
    uint32_t min_bytes = 1, max_bytes = AuthInteractionLimits::Input;
    bool digits_only = false;
    ~AuthInteractionField() { auth_interaction_wipe(label); }
};
struct AuthInteractionRequest {
    uint32_t protocol_version = AuthInteractionLimits::Version;
    uint64_t attempt_id = 0, downstream_attempt_id = 0, request_id = 0;
    uint32_t expires_after_ms = 0; // Remaining server deadline when dispatch starts; never extended.
    AuthInteractionKind kind = AuthInteractionKind::Unsupported;
    AuthInteractionUnsupported unsupported = AuthInteractionUnsupported::None;
    bool text_truncated = false;
    std::string title, message;
    std::vector<AuthInteractionButton> buttons;
    std::vector<AuthInteractionField> fields;
    AuthInteractionRequest() = default;
    AuthInteractionRequest(const AuthInteractionRequest&) = default;
    AuthInteractionRequest& operator=(const AuthInteractionRequest&) = default;
    AuthInteractionRequest(AuthInteractionRequest&&) = default;
    AuthInteractionRequest& operator=(AuthInteractionRequest&&) = default;
    ~AuthInteractionRequest() {
        auth_interaction_wipe(title); auth_interaction_wipe(message);
        for (auto& b : buttons) auth_interaction_wipe(b.label);
        for (auto& f : fields) auth_interaction_wipe(f.label);
    }
};
struct AuthInteractionValue {
    uint64_t field_id = 0;
    std::string value;
    AuthInteractionValue() = default;
    AuthInteractionValue(const AuthInteractionValue&) = default;
    AuthInteractionValue(AuthInteractionValue&&) = default;
    AuthInteractionValue& operator=(const AuthInteractionValue& other) {
        if (this != &other) { auth_interaction_wipe(value); field_id = other.field_id; value = other.value; }
        return *this;
    }
    AuthInteractionValue& operator=(AuthInteractionValue&& other) {
        if (this != &other) { auth_interaction_wipe(value); field_id = other.field_id; value = std::move(other.value); }
        return *this;
    }
    ~AuthInteractionValue() { auth_interaction_wipe(value); }
};
struct AuthInteractionResponse {
    uint32_t protocol_version = AuthInteractionLimits::Version;
    uint64_t attempt_id = 0, request_id = 0, button_id = 0;
    AuthInteractionDisposition disposition = AuthInteractionDisposition::Cancel;
    std::vector<AuthInteractionValue> values;
    AuthInteractionResponse() = default;
    AuthInteractionResponse(const AuthInteractionResponse&) = default;
    AuthInteractionResponse& operator=(const AuthInteractionResponse&) = default;
    AuthInteractionResponse(AuthInteractionResponse&&) = default;
    AuthInteractionResponse& operator=(AuthInteractionResponse&&) = default;
    ~AuthInteractionResponse() { for (auto& v : values) auth_interaction_wipe(v.value); }
};

// Capture shared ownership of an adapter/session. In-flight workers retain the
// function after replacement; requests contain no SDK references. Return promptly
// after posting UI work; submit an answer from any thread, including this handler.
using AuthInteractionHandler = std::function<void(AuthInteractionRequest)>;
bool set_auth_interaction_handler(AuthInteractionHandler handler,
    uint32_t timeout_ms = AuthInteractionLimits::DefaultTimeoutMs);
AuthInteractionSubmitResult submit_auth_interaction_response(AuthInteractionResponse response);
bool cancel_auth_interaction(uint64_t attempt_id);
// Zero outside an accepted login attempt; adapters may sample this on Started
// to support cancellation before the first UI prompt arrives.
uint64_t current_auth_interaction_attempt();
