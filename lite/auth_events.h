#pragma once

#include "auth_policy.h"
#include <cstdint>

// Explicit values are stable for adapters that serialize events to another process.
enum class AuthEventType : uint32_t {
    Started = 1,
    AccountChoice = 2,
    ExistingAccountSelected = 3,
    CredentialsRequested = 4,
    VerificationCodeRequested = 5,
    Response = 6,
    AuthenticationFailurePrompt = 7,
    UnsupportedDialog = 8,
    Completed = 9
};

// Value-only snapshot: no SDK objects, text, credentials or borrowed strings.
struct AuthEvent {
    AuthEventType type = AuthEventType::Started;
    AuthDiagnostics diagnostics;
    AuthNumber dialog_kind, dialog_failure_type;
    bool outcome_present = false; // true only for Completed
    AuthOutcome outcome = AuthOutcome::MissingResponse;
};

using AuthEventHandler = void (*)(const AuthEvent& event, void* user_data);

// nullptr unregisters. Called synchronously on the emitting thread with no
// wrapper locks held; the event may be copied. Handler exceptions are contained.
// A dispatch already in flight can still use the previous handler/user_data.
void set_auth_event_handler(AuthEventHandler handler, void* user_data = nullptr);
