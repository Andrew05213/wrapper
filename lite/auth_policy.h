#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Values are from the bundled storeservicescore, not HTTP status codes.
struct AuthNumber {
    bool present = false;
    int64_t value = 0;
    void set(int64_t n) { present = true; value = n; }
};

struct AuthDiagnostics {
    AuthNumber response_type, status, store_error, failure_type, protocol_error, http_status;
};

enum class AuthOutcome {
    Success = 0, AuthenticationFailed = 1, VerificationRequired = 2, Cancelled = 3,
    UnsupportedDialog = 4, InvalidCredentials = 5, InvalidVerificationCode = 6,
    TransportFailed = 7, MissingResponse = 8, UnexpectedResponse = 9, InternalError = 10
};

inline const char* auth_event(AuthOutcome outcome) {
    switch (outcome) {
    case AuthOutcome::Success: return "auth_authenticated";
    case AuthOutcome::AuthenticationFailed: return "auth_rejected";
    case AuthOutcome::VerificationRequired: return "auth_verification_required";
    case AuthOutcome::Cancelled: return "auth_cancelled";
    case AuthOutcome::UnsupportedDialog: return "auth_unsupported_dialog";
    case AuthOutcome::InvalidCredentials: return "auth_invalid_credentials";
    case AuthOutcome::InvalidVerificationCode: return "auth_invalid_verification_code";
    case AuthOutcome::TransportFailed: return "auth_transport_failed";
    case AuthOutcome::MissingResponse: return "auth_missing_response";
    case AuthOutcome::UnexpectedResponse: return "auth_unexpected_response";
    case AuthOutcome::InternalError: return "auth_internal_error";
    }
    return "auth_unexpected_response";
}

inline AuthOutcome classify_auth_response(const AuthDiagnostics& d) {
    if (!d.response_type.present) return AuthOutcome::MissingResponse;
    if (d.http_status.present && (d.http_status.value < 200 || d.http_status.value >= 300))
        return AuthOutcome::TransportFailed;
    // -5000 alone means neither an incorrect password nor a code challenge.
    if (d.response_type.value == 1 && d.failure_type.present && d.failure_type.value == -5000 &&
        d.protocol_error.present && d.protocol_error.value == -22947)
        return AuthOutcome::VerificationRequired;
    if (d.response_type.value == 5) return AuthOutcome::Cancelled;
    if ((d.store_error.present && d.store_error.value != 0) ||
        (d.failure_type.present && d.failure_type.value != 0) ||
        (d.protocol_error.present && d.protocol_error.value != 0) || d.response_type.value == 0)
        return AuthOutcome::AuthenticationFailed;
    if (d.response_type.value == 6 && (!d.status.present || d.status.value == 6)) return AuthOutcome::Success;
    return AuthOutcome::UnexpectedResponse;
}

// ProtocolAction::type() returns a protocol identifier, never a localized title.
struct AuthDialogButton { std::string action; };
inline int existing_account_button(int kind, int64_t failure, bool has_inputs,
                                   const std::vector<AuthDialogButton>& buttons) {
    if (kind != 2 || failure != 0 || has_inputs) return -1;
    int selected = -1;
    for (size_t i = 0; i < buttons.size(); ++i) {
        if (buttons[i].action == "authenticate") {
            if (selected != -1) return -1; // ambiguous: do not confirm
            selected = static_cast<int>(i);
        }
    }
    if (selected != -1) return selected;
    // _resolveEmptyAccount in this library builds kind=2, no text fields,
    // [Use Existing Apple ID, Create New Apple ID, Cancel], with no actions.
    // Its callback maps index 0 to existing-account result 2. No title matching.
    if (buttons.size() == 3 && buttons[0].action.empty() &&
        buttons[1].action.empty() && buttons[2].action.empty()) return 0;
    return -1;
}

inline bool valid_verification_code(const std::string& code) {
    return code.size() == 6 && code.find_first_not_of("0123456789") == std::string::npos;
}
