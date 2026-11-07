#include "internal.h"
#include "auth_diagnostics.h"
#include "library_ownership.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>
#include <atomic>

namespace {
std::mutex auth_state_mutex;
std::atomic<bool> login_running{false};
AuthDiagnostics observed;
void* observed_response = nullptr;
bool auth_active = false;
AuthOutcome outcome = AuthOutcome::MissingResponse;
bool callback_failed = false;
AuthDialogOrigin dialog_origin = AuthDialogOrigin::Protocol;
bool (*code_provider)(std::string&) = nullptr;
AuthEventHandler event_handler = nullptr;
void* event_user_data = nullptr;

AuthEvent auth_event_snapshot(AuthEventType type) {
    AuthEvent event;
    event.type = type;
    std::lock_guard<std::mutex> lock(auth_state_mutex);
    if (auth_active) event.diagnostics = observed;
    return event;
}

void dispatch_auth_event(const AuthEvent& event) {
    AuthEventHandler handler;
    void* user_data;
    {
        std::lock_guard<std::mutex> lock(auth_state_mutex);
        handler = event_handler;
        user_data = event_user_data;
    }
    if (handler) {
        try { handler(event, user_data); }
        catch (...) { LOG_WARN("auth_event_handler_failed"); }
    }
}

void wipe(std::string& s) {
    volatile char* p = s.empty() ? nullptr : &s[0];
    for (size_t i = 0; i < s.size(); ++i) p[i] = 0;
    s.clear();
}

void callback_failure(AuthOutcome reason) {
    std::lock_guard<std::mutex> lock(auth_state_mutex);
    if (!callback_failed) outcome = reason;
    callback_failed = true;
}

void log_diagnostics(const AuthDiagnostics& d) {
    if (d.response_type.present) LOG_INFO("auth_response_type=%lld", (long long)d.response_type.value);
    if (d.status.present) LOG_INFO("auth_status=%lld", (long long)d.status.value);
    if (d.store_error.present) LOG_INFO("auth_store_error=%lld", (long long)d.store_error.value);
    if (d.failure_type.present) LOG_INFO("auth_failure_type=%lld", (long long)d.failure_type.value);
    if (d.protocol_error.present) LOG_INFO("auth_protocol_error=%lld", (long long)d.protocol_error.value);
    else LOG_INFO("auth_protocol_error_unavailable");
    if (d.http_status.present) LOG_INFO("auth_http_status=%lld", (long long)d.http_status.value);
    else LOG_INFO("auth_http_status_unavailable");
}

bool read_code(std::string& code) {
    if (code_provider) return code_provider(code);
    FILE* input = nullptr;
    std::string path;
    LOG_INFO("auth_verification_code_requested");
    if (!g_code_from_file && isatty(STDIN_FILENO)) input = stdin;
    else {
        path = std::string(g_base_dir) + "/2fa.txt";
        int count = 0;
        while (!file_exists(path.c_str()) && count++ < 20) sleep(3);
        input = fopen(path.c_str(), "r");
    }
    if (!input) return false;
    bool complete = false, invalid = false;
    int ch;
    while ((ch = fgetc(input)) != EOF) {
        if (ch == '\n') { complete = true; break; }
        if (ch == 0 || code.size() >= 16) { invalid = true; break; }
        code.push_back(static_cast<char>(ch));
    }
    complete = complete || feof(input);
    if (input != stdin) { fclose(input); remove(path.c_str()); }
    if (!code.empty() && code.back() == '\r') code.pop_back();
    return !invalid && complete && !code.empty();
}
} // namespace

void set_verification_code_provider(bool (*provider)(std::string&)) { code_provider = provider; }

void set_auth_event_handler(AuthEventHandler handler, void* user_data) {
    std::lock_guard<std::mutex> lock(auth_state_mutex);
    event_handler = handler;
    event_user_data = handler ? user_data : nullptr;
}

AuthDialogOrigin set_auth_dialog_origin(AuthDialogOrigin origin) {
    std::lock_guard<std::mutex> lock(auth_state_mutex);
    const auto previous = dialog_origin;
    dialog_origin = origin;
    return previous;
}

AuthOutcome last_auth_outcome() {
    std::lock_guard<std::mutex> lock(auth_state_mutex);
    return outcome;
}

void auth_response_observed(void* response, struct shared_ptr* url_response) {
    const AuthDiagnostics d = inspect_auth_response(response, url_response);
    log_diagnostics(d);
    {
        std::lock_guard<std::mutex> lock(auth_state_mutex);
        if (auth_active) { observed = d; observed_response = response; }
    }
    AuthEvent event;
    event.type = AuthEventType::Response;
    event.diagnostics = d;
    dispatch_auth_event(event);
}

void dialogHandler(long j, struct shared_ptr* dialog, struct shared_ptr* handler) {
    (void)handler;
    auto* storage = static_cast<unsigned char*>(calloc(1, 72));
    if (!storage) throw std::bad_alloc();
    *(void**)storage = &_ZTVNSt6__ndk120__shared_ptr_emplaceIN17storeservicescore22ProtocolDialogResponseENS_9allocatorIS2_EEEE + 2;
    library_shared_ptr owner;
    owner.value = {storage + 24, storage};
    struct shared_ptr& response = owner.value;
    _ZN17storeservicescore22ProtocolDialogResponseC1Ev(response.obj);
    // Unsupported dialogs must still complete the callback, or the flow waits
    // forever. Never perform their default actions or select Retry/terms/URLs.
    _ZN17storeservicescore22ProtocolDialogResponse31setPerformsDefaultButtonActionsEb(response.obj, false);
    int selected = -1;
    AuthEvent event = auth_event_snapshot(AuthEventType::UnsupportedDialog);
    struct std_vector* buttons = nullptr;
    if (dialog && dialog->obj) {
        AuthDialogOrigin origin;
        { std::lock_guard<std::mutex> lock(auth_state_mutex); origin = dialog_origin; }
        // These two native flow methods construct local dialogs without setting
        // failureType. Initialize it only for those known local constructors;
        // never overwrite the field on a server-supplied protocol dialog.
        if (origin != AuthDialogOrigin::Protocol)
            _ZN17storeservicescore14ProtocolDialog14setFailureTypeEx(dialog->obj, 0);
        const int kind = _ZNK17storeservicescore14ProtocolDialog10dialogKindEv(dialog->obj);
        const long long failure = _ZNK17storeservicescore14ProtocolDialog11failureTypeEv(dialog->obj);
        event.dialog_kind.set(kind);
        event.dialog_failure_type.set(failure);
        const auto* fields = _ZNK17storeservicescore14ProtocolDialog10textFieldsEv(dialog->obj);
        buttons = _ZNK17storeservicescore14ProtocolDialog7buttonsEv(dialog->obj);
        std::vector<AuthDialogButton> semantics;
        if (buttons) {
            for (auto* b = static_cast<struct shared_ptr*>(buttons->begin);
                 b != static_cast<struct shared_ptr*>(buttons->end); ++b) {
                AuthDialogButton entry;
                if (!b->obj) entry.action = "unsupported";
                auto* action = b->obj ? _ZNK17storeservicescore14ProtocolButton6actionEv(b->obj) : nullptr;
                if (action && action->obj) {
                    // ProtocolAction::type() is its first virtual member.
                    using GetType = union std_string* (*)(void*);
                    auto get_type = reinterpret_cast<GetType>((*static_cast<void***>(action->obj))[0]);
                    auto* type = get_type(action->obj);
                    if (type) entry.action = std_string_data(type);
                }
                semantics.push_back(entry);
            }
        }
        if (origin == AuthDialogOrigin::AccountChoice)
            selected = existing_account_button(kind, failure, fields && fields->begin != fields->end, semantics);
        LOG_INFO("auth_dialog_kind=%d", kind);
        LOG_INFO("auth_dialog_failure_type=%lld", failure);
        if (origin == AuthDialogOrigin::AccountChoice) {
            event.type = AuthEventType::AccountChoice;
            dispatch_auth_event(event);
        }
    }
    if (selected >= 0) {
        LOG_INFO("dialog_existing_account_selected");
        _ZN17storeservicescore22ProtocolDialogResponse17setSelectedButtonERKNSt6__ndk110shared_ptrINS_14ProtocolButtonEEE(
            response.obj, static_cast<struct shared_ptr*>(buttons->begin) + selected);
        event.type = AuthEventType::ExistingAccountSelected;
    } else {
        bool rejected;
        { std::lock_guard<std::mutex> lock(auth_state_mutex);
          rejected = observed.response_type.present && observed.response_type.value == 0; }
        LOG_WARN("%s", rejected ? "dialog_auth_failure_prompt" : "dialog_unsupported");
        callback_failure(rejected ? AuthOutcome::AuthenticationFailed : AuthOutcome::UnsupportedDialog);
        event.type = rejected ? AuthEventType::AuthenticationFailurePrompt : AuthEventType::UnsupportedDialog;
    }
    dispatch_auth_event(event);
    _ZN20androidstoreservices28AndroidPresentationInterface28handleProtocolDialogResponseERKlRKNSt6__ndk110shared_ptrIN17storeservicescore22ProtocolDialogResponseEEE(
        apInf.obj, &j, &response);
}

void credentialHandler(struct shared_ptr* request, struct shared_ptr* handler) {
    (void)handler;
    LOG_INFO("credentials_requested");
    dispatch_auth_event(auth_event_snapshot(AuthEventType::CredentialsRequested));
    bool valid = amUsername && *amUsername && amPassword && *amPassword && request && request->obj;
    const bool needs_code = valid &&
        _ZNK17storeservicescore18CredentialsRequest28requiresHSA2VerificationCodeEv(request->obj);
    std::string password = valid ? amPassword : "";
    if (needs_code) {
        dispatch_auth_event(auth_event_snapshot(AuthEventType::VerificationCodeRequested));
        std::string code;
        valid = read_code(code) && valid_verification_code(code);
        if (valid) password += code; // Always start from the ORIGINAL password.
        else callback_failure(AuthOutcome::InvalidVerificationCode);
        wipe(code);
    } else if (!valid) callback_failure(AuthOutcome::InvalidCredentials);

    uint8_t* storage = static_cast<uint8_t*>(calloc(1, 80));
    if (!storage) { wipe(password); throw std::bad_alloc(); }
    *(void**)storage = &_ZTVNSt6__ndk120__shared_ptr_emplaceIN17storeservicescore19CredentialsResponseENS_9allocatorIS2_EEEE + 2;
    library_shared_ptr owner;
    owner.value = {storage + 24, storage};
    struct shared_ptr& response = owner.value;
    _ZN17storeservicescore19CredentialsResponseC1Ev(response.obj);
    if (valid) {
        union std_string username_view = new_std_string(amUsername);
        union std_string password_view = new_std_string(password.c_str());
        _ZN17storeservicescore19CredentialsResponse11setUserNameERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(response.obj, &username_view);
        _ZN17storeservicescore19CredentialsResponse11setPasswordERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(response.obj, &password_view);
    }
    wipe(password);
    // 0 = cancel, 2 = supplied credentials, as used by AuthenticateFlow.
    _ZN17storeservicescore19CredentialsResponse15setResponseTypeENS0_12ResponseTypeE(response.obj, valid ? 2 : 0);
    _ZN20androidstoreservices28AndroidPresentationInterface25handleCredentialsResponseERKNSt6__ndk110shared_ptrIN17storeservicescore19CredentialsResponseEEE(apInf.obj, &response);
}

void set_credentials(const char* user, const char* pass) {
    // Copy first, so callers may safely pass the current buffers again.
    char* new_user = user ? strdup(user) : nullptr;
    char* new_pass = pass ? strdup(pass) : nullptr;
    if ((user && !new_user) || (pass && !new_pass)) {
        free(new_user); free(new_pass); throw std::bad_alloc();
    }
    if (amUsername) { memset(amUsername, 0, strlen(amUsername)); free(amUsername); }
    if (amPassword) { memset(amPassword, 0, strlen(amPassword)); free(amPassword); }
    amUsername = new_user;
    amPassword = new_pass;
}

bool login(struct shared_ptr ctx) {
    // A callback can query state, replace itself, or attempt login again. Do not
    // keep a serialization mutex locked across user code or start a second flow.
    if (login_running.exchange(true)) {
        LOG_WARN("auth_login_busy");
        return false;
    }
    struct Finish {
        bool released = false;
        void release() {
            if (released) return;
            { std::lock_guard<std::mutex> lock(auth_state_mutex); auth_active = false; }
            login_running.store(false);
            released = true;
        }
        ~Finish() { release(); }
    } finish;
    {
        std::lock_guard<std::mutex> lock(auth_state_mutex);
        observed = {}; observed_response = nullptr; callback_failed = false;
        outcome = AuthOutcome::MissingResponse; auth_active = true;
    }
    LOG_INFO("auth_started");
    try {
        dispatch_auth_event(auth_event_snapshot(AuthEventType::Started));
        std::string storefront_path = std::string(g_base_dir) + "/STOREFRONT_ID";
        std::string token_path = std::string(g_base_dir) + "/MUSIC_TOKEN";
        remove(storefront_path.c_str()); remove(token_path.c_str());
        library_shared_ptr flow;
        _ZNSt6__ndk110shared_ptrIN17storeservicescore16AuthenticateFlowEE11make_sharedIJRNS0_INS1_14RequestContextEEEEEES3_DpOT_(&flow.value, &ctx);
        if (flow.value.obj) _ZN17storeservicescore16AuthenticateFlow3runEv(flow.value.obj);
        auto* response = flow.value.obj ? _ZNK17storeservicescore16AuthenticateFlow8responseEv(flow.value.obj) : nullptr;
        AuthDiagnostics d;
        {
            std::lock_guard<std::mutex> lock(auth_state_mutex);
            if (response && response->obj == observed_response) d = observed;
        }
        if (response && response->obj && !d.response_type.present) {
            // Local failures have no URLResponse: leave protocol/HTTP unavailable.
            d = inspect_auth_response(response->obj, nullptr);
            log_diagnostics(d);
            {
                std::lock_guard<std::mutex> lock(auth_state_mutex);
                observed = d;
            }
            AuthEvent event;
            event.type = AuthEventType::Response;
            event.diagnostics = d;
            dispatch_auth_event(event);
        }
        std::lock_guard<std::mutex> lock(auth_state_mutex);
        if (!callback_failed) outcome = classify_auth_response(d);
        LOG_INFO("%s", auth_event(outcome));
    } catch (...) {
        std::lock_guard<std::mutex> lock(auth_state_mutex);
        outcome = AuthOutcome::InternalError;
        LOG_ERROR("auth_internal_error");
    }
    AuthEvent completed;
    completed.type = AuthEventType::Completed;
    completed.outcome_present = true;
    {
        std::lock_guard<std::mutex> lock(auth_state_mutex);
        completed.diagnostics = observed;
        completed.outcome = outcome;
    }
    // Native flow owners have already been destroyed. Release the attempt gate
    // before the final notification, allowing a subsequent attempt from it.
    finish.release();
    dispatch_auth_event(completed);
    return completed.outcome == AuthOutcome::Success;
}
