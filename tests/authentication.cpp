#include "../lite/auth.cpp"
#include <cassert>
#include <map>
#include <fstream>
#include <type_traits>

static_assert(std::is_trivially_copyable<AuthEvent>::value, "events must remain value-only snapshots");

char* amUsername = nullptr;
char* amPassword = nullptr;
char g_base_dir[256] = "/tmp/wrapper-auth-fixture-unused";
bool g_code_from_file = true;
struct shared_ptr apInf = {};
void* _ZTVNSt6__ndk120__shared_ptr_emplaceIN17storeservicescore22ProtocolDialogResponseENS_9allocatorIS2_EEEE = nullptr;

struct Number { bool string; std::string text; int64_t number; };
static Number protocol_error = {true, "-22947", 0};
static bool has_protocol_error = true;
static int native_type = 0, native_status = 6, store_error = 10;
static long failure_type = -5000;
static bool has_error = true, has_response = true, show_unsupported = false, throw_exception = false;
static bool observe_response = true;
static char http[136] = {};
static struct shared_ptr native_response = {&native_type, nullptr};
static struct shared_ptr url_response = {http, nullptr};
static struct shared_ptr error = {&store_error, nullptr};
static int kind = 2;
static long long dialog_failure = 0;
static struct shared_ptr button_array[3] = {{http, nullptr}, {http, nullptr}, {http, nullptr}};
static struct std_vector buttons = {button_array, button_array + 3, button_array + 3};
static struct std_vector fields = {};
static int selection = -1;
static bool default_actions = true;

extern "C" const void* CFStringCreateWithCString(const void*, const char* text, uint32_t) { return strdup(text); }
extern "C" void CFRelease(const void* p) { if (p != http) free(const_cast<void*>(p)); }
extern "C" const void* CFDictionaryGetValue(const void*, const void* key) {
    assert(strcmp(static_cast<const char*>(key), "errorCode") == 0);
    return has_protocol_error ? &protocol_error : nullptr;
}
extern "C" unsigned long CFGetTypeID(const void* p) { return static_cast<const Number*>(p)->string ? 2 : 1; }
extern "C" unsigned long CFNumberGetTypeID() { return 1; }
extern "C" unsigned long CFStringGetTypeID() { return 2; }
extern "C" unsigned char CFNumberGetValue(const void* p, int type, void* out) {
    assert(type == 4); *static_cast<int64_t*>(out) = static_cast<const Number*>(p)->number; return 1;
}
extern "C" unsigned char CFStringGetCString(const void* p, char* out, long size, uint32_t) {
    const auto& text = static_cast<const Number*>(p)->text;
    if (text.size() >= static_cast<size_t>(size)) return 0;
    strcpy(out, text.c_str()); return 1;
}
extern "C" void _ZN17storeservicescore11URLResponse18protocolDictionaryEv(void* out, void*) { *static_cast<const void**>(out) = http; }
extern "C" struct shared_ptr* _ZNK17storeservicescore11URLResponse18underlyingResponseEv(void*) { return &url_response; }
extern "C" int _ZNK17storeservicescore20AuthenticateResponse12responseTypeEv(void*) { return native_type; }
extern "C" const int* _ZNK17storeservicescore20AuthenticateResponse6statusEv(void*) { return &native_status; }
extern "C" struct shared_ptr* _ZNK17storeservicescore20AuthenticateResponse5errorEv(void*) { return has_error ? &error : nullptr; }
extern "C" const int* _ZNK17storeservicescore19StoreErrorCondition9errorCodeEv(void*) { return &store_error; }
extern "C" const long* _ZNK17storeservicescore19StoreErrorCondition17externalErrorCodeEv(void*) { return &failure_type; }
extern "C" void _ZNSt6__ndk119__shared_weak_count16__release_sharedEv(void* p) { free(p); }
extern "C" void _ZNSt6__ndk110shared_ptrIN17storeservicescore16AuthenticateFlowEE11make_sharedIJRNS0_INS1_14RequestContextEEEEEES3_DpOT_(struct shared_ptr* out, struct shared_ptr*) { *out = {http, nullptr}; }
extern "C" struct shared_ptr* _ZNK17storeservicescore16AuthenticateFlow8responseEv(void*) { return has_response ? &native_response : nullptr; }
extern "C" void _ZN17storeservicescore16AuthenticateFlow3runEv(void*) {
    if (throw_exception) throw std::runtime_error("SECRET-native-exception");
    if (has_response && observe_response) auth_response_observed(native_response.obj, &url_response);
    if (show_unsupported) { struct shared_ptr dialog = {http, nullptr}; dialogHandler(1, &dialog, nullptr); }
}
extern "C" void _ZN17storeservicescore22ProtocolDialogResponseC1Ev(void*) { selection = -1; default_actions = true; }
extern "C" void _ZN17storeservicescore22ProtocolDialogResponse31setPerformsDefaultButtonActionsEb(void*, bool value) { default_actions = value; }
extern "C" int _ZNK17storeservicescore14ProtocolDialog10dialogKindEv(void*) { return kind; }
extern "C" union std_string* _ZNK17storeservicescore14ProtocolDialog5titleEv(void*) {
    static union std_string value = new_std_string("登录"); return &value;
}
extern "C" union std_string* _ZNK17storeservicescore14ProtocolDialog7messageEv(void*) {
    static union std_string value = new_std_string("SECRET-localized-server-message"); return &value;
}
extern "C" union std_string* _ZNK17storeservicescore14ProtocolButton5titleEv(void*) {
    static union std_string value = new_std_string("使用现有 Apple ID"); return &value;
}
extern "C" long long _ZNK17storeservicescore14ProtocolDialog11failureTypeEv(void*) { return dialog_failure; }
extern "C" void _ZN17storeservicescore14ProtocolDialog14setFailureTypeEx(void*, long long value) { dialog_failure = value; }
extern "C" struct std_vector* _ZNK17storeservicescore14ProtocolDialog10textFieldsEv(void*) { return &fields; }
extern "C" struct std_vector* _ZNK17storeservicescore14ProtocolDialog7buttonsEv(void*) { return &buttons; }
extern "C" struct shared_ptr* _ZNK17storeservicescore14ProtocolButton6actionEv(void*) { return nullptr; }
extern "C" void _ZN17storeservicescore22ProtocolDialogResponse17setSelectedButtonERKNSt6__ndk110shared_ptrINS_14ProtocolButtonEEE(void*, struct shared_ptr* button) { selection = static_cast<int>(button - button_array); }
extern "C" void _ZN20androidstoreservices28AndroidPresentationInterface28handleProtocolDialogResponseERKlRKNSt6__ndk110shared_ptrIN17storeservicescore22ProtocolDialogResponseEEE(void*, long*, struct shared_ptr*) { assert(!default_actions); }

struct EventRecorder {
    std::vector<AuthEvent> events;
    bool throw_on_response = false;
    bool login_after_completion = false;
};
static void record_event(const AuthEvent& event, void* context) {
    auto& recorder = *static_cast<EventRecorder*>(context);
    recorder.events.push_back(event); // Consumers can retain a copy safely.
    const auto current = last_auth_outcome(); // Reentrant state access.
    set_auth_event_handler(record_event, context); // Reentrant registration.
    assert(event.outcome_present == (event.type == AuthEventType::Completed));
    if (event.type == AuthEventType::Started) {
        const size_t count = recorder.events.size();
        assert(!login({nullptr, nullptr})); // In-progress reentry must return, not deadlock.
        assert(recorder.events.size() == count); // No recursive busy-event loop.
    }
    if (event.outcome_present) assert(event.outcome == current);
    if (event.type == AuthEventType::Response && recorder.throw_on_response) {
        recorder.throw_on_response = false;
        throw std::runtime_error("SECRET-event-handler-exception");
    }
    if (event.type == AuthEventType::Completed && recorder.login_after_completion) {
        recorder.login_after_completion = false;
        native_type = 6; has_error = false; has_protocol_error = false;
        assert(login({nullptr, nullptr})); // Final dispatch releases the attempt gate.
    }
}
static EventRecorder recorder;
static void expect(AuthOutcome expected) {
    recorder.events.clear();
    assert(login({nullptr, nullptr}) == (expected == AuthOutcome::Success));
    assert(last_auth_outcome() == expected);
    assert(recorder.events.front().type == AuthEventType::Started);
    const auto& final = recorder.events.back();
    assert(final.type == AuthEventType::Completed && final.outcome == expected);
    assert(final.outcome_present);
    if (has_response && !throw_exception) {
        assert(final.diagnostics.response_type.present && final.diagnostics.response_type.value == native_type);
        assert(final.diagnostics.http_status.present == observe_response);
        assert(final.diagnostics.store_error.present == has_error);
        if (has_error) {
            assert(final.diagnostics.store_error.value == store_error);
            assert(final.diagnostics.failure_type.present && final.diagnostics.failure_type.value == failure_type);
        }
    } else assert(!final.diagnostics.response_type.present);
}
int main() {
    char directory[] = "/tmp/wrapper-auth-XXXXXX";
    assert(mkdtemp(directory));
    snprintf(g_base_dir, sizeof(g_base_dir), "%s", directory);
    const std::string log = std::string(directory) + "/events.log";
    Logger::get().configure(LogLevel::Debug, log, 0);
    set_auth_event_handler(record_event, &recorder);
    int status = 200; memcpy(http + 128, &status, sizeof(status));
    // Same failureType, different protocol errorCode and responseType.
    protocol_error.text = "-5000"; expect(AuthOutcome::AuthenticationFailed);
    assert(recorder.events[1].type == AuthEventType::Response);
    assert(recorder.events.back().diagnostics.protocol_error.present);
    assert(recorder.events.back().diagnostics.protocol_error.value == -5000);
    native_type = 1; protocol_error.text = "-22947"; expect(AuthOutcome::VerificationRequired);
    assert(recorder.events.back().diagnostics.protocol_error.value == -22947);
    native_type = 0; expect(AuthOutcome::AuthenticationFailed); // never infer challenge from -5000
    native_type = 1; has_protocol_error = false; expect(AuthOutcome::AuthenticationFailed);
    has_protocol_error = true; protocol_error.text = "SECRET-server-message";
    assert(!auth_protocol_number(http, "errorCode").present);
    protocol_error.text = "-22947-secret"; assert(!auth_protocol_number(http, "errorCode").present);
    protocol_error.string = false; protocol_error.number = -22947;
    expect(AuthOutcome::VerificationRequired);
    status = 401; memcpy(http + 128, &status, sizeof(status)); expect(AuthOutcome::TransportFailed);
    native_type = 6; has_error = false; has_protocol_error = false;
    expect(AuthOutcome::TransportFailed); // native success cannot hide HTTP rejection
    status = 200; memcpy(http + 128, &status, sizeof(status)); expect(AuthOutcome::Success);
    observe_response = false; expect(AuthOutcome::Success);
    assert(!recorder.events.back().diagnostics.protocol_error.present);
    observe_response = true;
    native_status = 2; expect(AuthOutcome::UnexpectedResponse); native_status = 6;
    has_response = false; expect(AuthOutcome::MissingResponse);
    has_response = true; native_type = 4; expect(AuthOutcome::UnexpectedResponse);

    struct shared_ptr dialog = {http, nullptr};
    set_auth_dialog_origin(AuthDialogOrigin::AccountChoice);
    dialog_failure = 0x5a5a5a5a5a5a5a5aLL; // stale bytes from the native default constructor
    kind = 2; dialogHandler(1, &dialog, nullptr); assert(selection == 0);
    assert(recorder.events[recorder.events.size() - 2].type == AuthEventType::AccountChoice);
    assert(recorder.events.back().type == AuthEventType::ExistingAccountSelected);
    assert(recorder.events.back().dialog_kind.present && recorder.events.back().dialog_kind.value == 2);
    assert(recorder.events.back().dialog_failure_type.present && recorder.events.back().dialog_failure_type.value == 0);
    assert(dialog_failure == 0);
    // Chinese titles cannot change selection; server text must never enter logs.
    kind = 0; dialogHandler(1, &dialog, nullptr); assert(selection == -1);
    set_auth_dialog_origin(AuthDialogOrigin::Protocol);
    kind = 2; dialog_failure = -5000; dialogHandler(1, &dialog, nullptr); assert(selection == -1);
    assert(recorder.events.back().type == AuthEventType::UnsupportedDialog);
    assert(recorder.events.back().dialog_failure_type.value == -5000);
    dialog_failure = 0; fields.begin = http; fields.end = http + 1;
    dialogHandler(1, &dialog, nullptr); assert(selection == -1);
    fields = {}; kind = 0; show_unsupported = true;
    native_type = 6; expect(AuthOutcome::UnsupportedDialog); // callback error overrides flow success
    native_type = 0; expect(AuthOutcome::AuthenticationFailed); // Cancel/Retry is downstream failure
    assert(recorder.events[recorder.events.size() - 2].type == AuthEventType::AuthenticationFailurePrompt);
    assert(recorder.events[recorder.events.size() - 2].diagnostics.response_type.value == 0);
    assert(selection == -1);
    show_unsupported = false; throw_exception = true;
    expect(AuthOutcome::InternalError); throw_exception = false;
    native_type = 6; recorder.throw_on_response = true;
    expect(AuthOutcome::Success); // Exceptions in observers cannot alter authentication.
    native_type = 0; recorder.login_after_completion = true;
    recorder.events.clear();
    assert(!login({nullptr, nullptr})); // Outer return is not overwritten by the nested success.
    assert(recorder.events[2].type == AuthEventType::Completed);
    assert(recorder.events[2].outcome == AuthOutcome::AuthenticationFailed);
    assert(recorder.events.back().outcome == AuthOutcome::Success);
    assert(last_auth_outcome() == AuthOutcome::Success);
    const size_t count = recorder.events.size();
    set_auth_event_handler(nullptr);
    assert(login({nullptr, nullptr}));
    assert(recorder.events.size() == count);
    std::vector<AuthDialogButton> semantic = {{"open-url"}, {"authenticate"}};
    assert(existing_account_button(2, 0, false, semantic) == 1);
    semantic.push_back({"authenticate"});
    assert(existing_account_button(2, 0, false, semantic) == -1);
    std::ifstream input(log); std::string contents((std::istreambuf_iterator<char>(input)), {});
    assert(contents.find("SECRET") == std::string::npos);
    assert(contents.find("auth_protocol_error=-22947") != std::string::npos);
    assert(contents.find("auth_http_status=401") != std::string::npos);
    remove(log.c_str()); rmdir(directory);
    puts("auth: protocol fields, transport status, dialog cancellation and no false success passed");
}
