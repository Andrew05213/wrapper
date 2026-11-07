#define WRAPPER_AUTH_FIXTURE_WITH_INTERACTION
#define WRAPPER_AUTH_FIXTURE_NO_MAIN
#include "authentication.cpp"
#include "../lite/auth_sdk_interaction.cpp"
#include <condition_variable>
#include <future>
#include <thread>

struct MockField { int keyboard; bool secure; std::string label; };
static std::vector<struct shared_ptr> field_objects;
static MockField account_field{1, false, "账号"}, password_field{0, true, "密码"};
static MockField text_field{0, false, "必要说明"}, number_field{2, false, "数字"};
static bool sdk_code = false, sdk_biometric = false;
static std::string sdk_ok_action = "authenticate", supplied_user, supplied_password;
static int credentials_type = 0, credentials_completions = 0, provided_completions = 0;
static std::thread::id sdk_thread;
static std::vector<std::string> submissions;
int file_exists(const char* path) { FILE* fp = fopen(path, "r"); if (!fp) return 0; fclose(fp); return 1; }
void* _ZTVNSt6__ndk120__shared_ptr_emplaceIN17storeservicescore19CredentialsResponseENS_9allocatorIS2_EEEE = nullptr;

extern "C" uint8_t _ZNK17storeservicescore14ProtocolDialog29allowsBiometricAuthenticationEv(void*) { return sdk_biometric; }
extern "C" int _ZNK17storeservicescore17ProtocolTextField12keyboardTypeEv(void* p) { return static_cast<MockField*>(p)->keyboard; }
extern "C" uint8_t _ZNK17storeservicescore17ProtocolTextField8isSecureEv(void* p) { return static_cast<MockField*>(p)->secure; }
extern "C" union std_string* _ZNK17storeservicescore17ProtocolTextField5labelEv(void* p) {
    static union std_string view; view = new_std_string(static_cast<MockField*>(p)->label.c_str()); return &view;
}
extern "C" void _ZN17storeservicescore22ProtocolDialogResponse17setTextFieldValueERKNSt6__ndk110shared_ptrINS_17ProtocolTextFieldEEERKNS1_12basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void*, struct shared_ptr* field, union std_string* v) {
    sdk_field_values[field->obj] = std_string_data(v);
}
extern "C" uint8_t _ZNK17storeservicescore18CredentialsRequest28requiresHSA2VerificationCodeEv(void*) { return sdk_code; }
extern "C" union std_string* _ZNK17storeservicescore18CredentialsRequest5titleEv(void* p) { return _ZNK17storeservicescore14ProtocolDialog5titleEv(p); }
extern "C" union std_string* _ZNK17storeservicescore18CredentialsRequest7messageEv(void* p) { return _ZNK17storeservicescore14ProtocolDialog7messageEv(p); }
extern "C" union std_string* _ZNK17storeservicescore18CredentialsRequest13okButtonTitleEv(void*) {
    static union std_string view = new_std_string("明确提交"); return &view;
}
extern "C" union std_string* _ZNK17storeservicescore18CredentialsRequest17cancelButtonTitleEv(void*) {
    static union std_string view = new_std_string("取消"); return &view;
}
extern "C" struct shared_ptr* _ZNK17storeservicescore18CredentialsRequest14okButtonActionEv(void*) {
    static void* table[1] = {reinterpret_cast<void*>(+[](void*) -> union std_string* {
        static union std_string view; view = new_std_string(sdk_ok_action.c_str()); return &view;
    })};
    static void** pointer = table;
    static struct shared_ptr action = {&pointer, nullptr}; return &action;
}
extern "C" void _ZNK17storeservicescore18CredentialsRequest6dialogEv(void* out, void*) {
    *static_cast<struct shared_ptr*>(out) = {http, nullptr};
}
extern "C" void _ZN17storeservicescore19CredentialsResponseC1Ev(void*) { supplied_user.clear(); supplied_password.clear(); }
extern "C" void _ZN17storeservicescore19CredentialsResponse11setUserNameERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void*, union std_string* v) { supplied_user = std_string_data(v); }
extern "C" void _ZN17storeservicescore19CredentialsResponse11setPasswordERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void*, union std_string* v) { supplied_password = std_string_data(v); }
extern "C" void _ZN17storeservicescore19CredentialsResponse15setResponseTypeENS0_12ResponseTypeE(void*, int type) { credentials_type = type; }
extern "C" void _ZN20androidstoreservices28AndroidPresentationInterface25handleCredentialsResponseERKNSt6__ndk110shared_ptrIN17storeservicescore19CredentialsResponseEEE(void*, struct shared_ptr*) {
    ++credentials_completions; submissions.push_back(supplied_password);
}

static void* handler_table[3];
static struct shared_ptr owned_request, owned_handler;
static void credentials_complete(void*, struct shared_ptr* response) {
    assert(std::this_thread::get_id() == sdk_thread);
    ++provided_completions;
    _ZN20androidstoreservices28AndroidPresentationInterface25handleCredentialsResponseERKNSt6__ndk110shared_ptrIN17storeservicescore19CredentialsResponseEEE(nullptr, response);
}
static void dialog_complete(void*, long* id, struct shared_ptr* response) {
    assert(std::this_thread::get_id() == sdk_thread);
    ++provided_completions;
    _ZN20androidstoreservices28AndroidPresentationInterface28handleProtocolDialogResponseERKlRKNSt6__ndk110shared_ptrIN17storeservicescore22ProtocolDialogResponseEEE(nullptr, id, response);
}
static void make_owned(bool credential) {
    sdk_thread = std::this_thread::get_id();
    handler_table[2] = reinterpret_cast<void*>(credential ? reinterpret_cast<void(*)()>(credentials_complete) : reinterpret_cast<void(*)()>(dialog_complete));
    owned_request = {http, calloc(1, 24)};
    static void** vptr;
    vptr = handler_table;
    owned_handler = {&vptr, calloc(1, 24)};
}
static void release_owned() {
    assert(reinterpret_cast<long*>(owned_request.ctrl_blk)[1] == 0);
    assert(reinterpret_cast<long*>(owned_handler.ctrl_blk)[1] == 0);
    _ZNSt6__ndk119__shared_weak_count16__release_sharedEv(owned_request.ctrl_blk);
    _ZNSt6__ndk119__shared_weak_count16__release_sharedEv(owned_handler.ctrl_blk);
    owned_request = {}; owned_handler = {};
}
static void set_fields(std::initializer_list<MockField*> items) {
    field_objects.clear();
    for (auto* field : items) field_objects.push_back({field, nullptr});
    fields = {field_objects.data(), field_objects.data() + field_objects.size(), field_objects.data() + field_objects.size()};
}
static void success_response() {
    native_type = 6; native_status = 6; has_error = false; has_protocol_error = false;
    auth_response_observed(native_response.obj, &url_response);
}
static void run_dialog() { dialogHandler(81, &owned_request, &owned_handler); success_response(); }
static void run_credentials() { credentialHandler(&owned_request, &owned_handler); success_response(); }
static void workers_done() {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (auth_interaction_worker_count() && std::chrono::steady_clock::now() < end) std::this_thread::yield();
    assert(!auth_interaction_worker_count());
}
static AuthInteractionResponse submit_for(const AuthInteractionRequest& q, size_t button = 0) {
    AuthInteractionResponse a; a.attempt_id = q.attempt_id; a.request_id = q.request_id;
    a.disposition = AuthInteractionDisposition::Submit; a.button_id = q.buttons[button].id;
    for (const auto& f : q.fields) {
        AuthInteractionValue value; value.field_id = f.id;
        value.value = f.digits_only ? "654321" : "SECRET-private-input";
        a.values.push_back(std::move(value));
    }
    return a;
}
static void expect_ui_failure(AuthOutcome expected) {
    const int before = provided_completions;
    assert(!login({nullptr, nullptr}, 999));
    assert(last_auth_outcome() == expected);
    assert(provided_completions == before + 1); // Exactly one completion on every error.
    workers_done();
}

int main() {
    char directory[] = "/tmp/wrapper-ui-sdk-XXXXXX"; assert(mkdtemp(directory));
    snprintf(g_base_dir, sizeof(g_base_dir), "%s", directory);
    const std::string log = std::string(directory) + "/events.log";
    Logger::get().configure(LogLevel::Debug, log, 0);
    const int status = 200; memcpy(http + 128, &status, sizeof(status));
    for (int i = 0; i < 3; ++i) button_array[i].obj = http + i;
    sdk_button_titles[http] = "继续"; sdk_button_titles[http + 1] = "重试"; sdk_button_titles[http + 2] = "取消";
    kind = 2; set_fields({}); set_auth_dialog_origin(AuthDialogOrigin::AccountChoice);
    buttons.end = button_array + 3;
    assert(set_auth_interaction_handler([](AuthInteractionRequest q) {
        assert(std::this_thread::get_id() != sdk_thread && q.expires_after_ms > 0);
        assert(q.title == "登录" && q.message == "SECRET-localized-server-message");
        assert(q.kind == AuthInteractionKind::AccountChoice && q.downstream_attempt_id == 999);
        assert(!q.buttons[1].enabled && q.buttons[2].role == AuthInteractionButtonRole::Cancel);
        assert(submit_auth_interaction_response(submit_for(q, 1)) == AuthInteractionSubmitResult::InvalidResponse);
        assert(submit_auth_interaction_response(submit_for(q)) == AuthInteractionSubmitResult::Accepted);
    }, 1000));
    make_owned(false); flow_script = run_dialog;
    assert(login({nullptr, nullptr}, 999) && selection == 0); release_owned(); workers_done();

    // Retry selection uses only the offered ID. No failure latch until cancellation.
    kind = 0; buttons.end = button_array + 2; set_auth_dialog_origin(AuthDialogOrigin::FailurePrompt);
    for (size_t chosen : {1u, 0u}) {
        assert(set_auth_interaction_handler([chosen](AuthInteractionRequest q) {
            assert(q.kind == AuthInteractionKind::AuthenticationFailure);
            assert(q.buttons[1].role == AuthInteractionButtonRole::Retry);
            assert(submit_auth_interaction_response(submit_for(q, chosen)) == AuthInteractionSubmitResult::Accepted);
        }, 1000));
        make_owned(false);
        const bool ok = login({nullptr, nullptr}, 999);
        assert(ok == (chosen == 1) && selection == static_cast<int>(chosen));
        if (!ok) assert(last_auth_outcome() == AuthOutcome::Cancelled);
        release_owned(); workers_done();
    }
    set_auth_dialog_origin(AuthDialogOrigin::Protocol); kind = 0;
    set_fields({&text_field, &number_field});
    assert(set_auth_interaction_handler([](AuthInteractionRequest q) {
        assert(q.kind == AuthInteractionKind::ProtocolDialog && q.fields.size() == 2);
        assert(q.fields[1].type == AuthInteractionFieldType::Digits);
        assert(submit_auth_interaction_response(submit_for(q)) == AuthInteractionSubmitResult::Accepted);
    }, 1000));
    make_owned(false); assert(login({nullptr, nullptr}, 999));
    assert(sdk_field_values[&text_field] == "SECRET-private-input" && sdk_field_values[&number_field] == "654321");
    release_owned(); workers_done();

    // Credentials + two fresh verification challenges in one attempt.
    set_fields({&account_field, &password_field}); sdk_code = false;
    int interactions = 0;
    std::string ui_original_password = " SECRET:pass\\\"&<密码> ";
    assert(set_auth_interaction_handler([&](AuthInteractionRequest q) {
        ++interactions;
        auto a = submit_for(q);
        if (q.kind == AuthInteractionKind::Credentials) {
            a.values[0].value = "fixture@example.invalid";
            a.values[1].value = ui_original_password;
        } else {
            assert(q.kind == AuthInteractionKind::VerificationCode);
            auto invalid = a; invalid.values[0].value = "abcdef";
            assert(submit_auth_interaction_response(std::move(invalid)) == AuthInteractionSubmitResult::InvalidResponse);
            a.values[0].value = interactions == 2 ? "123456" : "987654";
        }
        assert(submit_auth_interaction_response(std::move(a)) == AuthInteractionSubmitResult::Accepted);
    }, 1000));
    make_owned(true);
    flow_script = +[] {
        sdk_code = false; credentialHandler(&owned_request, &owned_handler);
        assert(credentials_type == 2);
        sdk_code = true; credentialHandler(&owned_request, &owned_handler);
        assert(credentials_type == 2);
        credentialHandler(&owned_request, &owned_handler); success_response();
    };
    assert(login({nullptr, nullptr}, 999));
    assert(interactions == 3 && submissions[submissions.size() - 2] == submissions[submissions.size() - 3] + "123456");
    assert(submissions.back() == submissions[submissions.size() - 3] + "987654");
    assert(!amPassword); // UI answers did not replace or persist CLI credential globals.
    std::string user, password;
    assert(!auth_interaction_get_credentials(user, password));
    release_owned(); workers_done();
    interactions = 0; ui_original_password.assign(4096, 'x');
    make_owned(true); assert(login({nullptr, nullptr}, 999));
    assert(submissions.back() == ui_original_password + "987654");
    release_owned(); workers_done();

    flow_script = run_dialog; sdk_code = false; set_fields({&text_field});
    // Unknown field metadata is shown, acknowledged as unsupported, never guessed.
    text_field.keyboard = 999;
    assert(set_auth_interaction_handler([](AuthInteractionRequest q) {
        assert(q.kind == AuthInteractionKind::Unsupported && !q.message.empty());
        AuthInteractionResponse a; a.attempt_id = q.attempt_id; a.request_id = q.request_id;
        a.disposition = AuthInteractionDisposition::Unsupported;
        assert(submit_auth_interaction_response(std::move(a)) == AuthInteractionSubmitResult::Accepted);
    }, 1000));
    make_owned(false); expect_ui_failure(AuthOutcome::UnsupportedDialog); release_owned(); text_field.keyboard = 0;

    struct MockAction { void** vtable; } action;
    void* action_table[1];
    action_table[0] = reinterpret_cast<void*>(+[](void*) -> union std_string* {
        static union std_string type = new_std_string("open-url"); return &type;
    });
    action.vtable = action_table; sdk_actions[http] = {&action, nullptr};
    make_owned(false); expect_ui_failure(AuthOutcome::UnsupportedDialog); release_owned(); sdk_actions.clear();
    sdk_biometric = true;
    make_owned(false); expect_ui_failure(AuthOutcome::UnsupportedDialog); release_owned(); sdk_biometric = false;
    kind = 999;
    make_owned(false); expect_ui_failure(AuthOutcome::UnsupportedDialog); release_owned(); kind = 0;
    sdk_title.assign(AuthInteractionLimits::Title + 1, 'x');
    assert(set_auth_interaction_handler([](AuthInteractionRequest q) {
        assert(q.kind == AuthInteractionKind::Unsupported && q.unsupported == AuthInteractionUnsupported::Limits);
        assert(q.title.size() <= AuthInteractionLimits::Title && q.buttons.size() <= 16 && q.fields.size() <= 16);
        AuthInteractionResponse a; a.attempt_id = q.attempt_id; a.request_id = q.request_id;
        a.disposition = AuthInteractionDisposition::Unsupported;
        assert(submit_auth_interaction_response(std::move(a)) == AuthInteractionSubmitResult::Accepted);
    }, 1000));
    make_owned(false); expect_ui_failure(AuthOutcome::UnsupportedDialog); release_owned(); sdk_title = "登录";
    std::vector<MockField> many_fields(17, text_field);
    field_objects.clear(); for (auto& f : many_fields) field_objects.push_back({&f, nullptr});
    fields = {field_objects.data(), field_objects.data() + field_objects.size(), field_objects.data() + field_objects.size()};
    make_owned(false); expect_ui_failure(AuthOutcome::UnsupportedDialog); release_owned(); set_fields({&text_field});
    std::vector<struct shared_ptr> many_buttons(17, {http, nullptr});
    const auto saved_buttons = buttons;
    buttons = {many_buttons.data(), many_buttons.data() + many_buttons.size(), many_buttons.data() + many_buttons.size()};
    make_owned(false); expect_ui_failure(AuthOutcome::UnsupportedDialog); release_owned(); buttons = saved_buttons;

    // Missing originals for a code challenge cannot be disguised as a password attempt.
    sdk_code = true; flow_script = run_credentials;
    assert(set_auth_interaction_handler([](AuthInteractionRequest q) {
        assert(q.kind == AuthInteractionKind::Unsupported && q.unsupported == AuthInteractionUnsupported::MissingCredentials);
        AuthInteractionResponse a; a.attempt_id = q.attempt_id; a.request_id = q.request_id;
        a.disposition = AuthInteractionDisposition::Unsupported;
        assert(submit_auth_interaction_response(std::move(a)) == AuthInteractionSubmitResult::Accepted);
    }, 1000));
    make_owned(true); expect_ui_failure(AuthOutcome::UnsupportedDialog); release_owned(); sdk_code = false;
    assert(credentials_type == 0 && supplied_password.empty());
    flow_script = run_dialog;

    // Cancellation on Started, before the first prompt ID, needs no SDK/UI round trip.
    assert(set_auth_interaction_handler([](AuthInteractionRequest) { assert(false); }, 1000));
    set_auth_event_handler(+[](const AuthEvent& event, void*) {
        if (event.type == AuthEventType::Started)
            assert(cancel_auth_interaction(current_auth_interaction_attempt()));
    });
    make_owned(false); expect_ui_failure(AuthOutcome::Cancelled); release_owned(); set_auth_event_handler(nullptr);

    for (int mode = 0; mode < 3; ++mode) {
        assert(set_auth_interaction_handler([mode](AuthInteractionRequest q) {
            (void)last_auth_outcome(); // No state mutex held across downstream invocation.
            if (mode == 0) {
                AuthInteractionResponse a; a.attempt_id = q.attempt_id; a.request_id = q.request_id;
                assert(submit_auth_interaction_response(std::move(a)) == AuthInteractionSubmitResult::Accepted);
            } else if (mode == 1) set_auth_interaction_handler({});
            else throw std::runtime_error("SECRET-handler-exception");
        }, 1000));
        make_owned(false);
        expect_ui_failure(mode == 0 ? AuthOutcome::Cancelled : mode == 1 ? AuthOutcome::InteractionUnavailable : AuthOutcome::InternalError);
        release_owned();
    }
    // A worker can outlive SDK objects after timeout; it retains only copied UI values.
    auto gate = std::make_shared<std::promise<void>>(); auto ready = gate->get_future().share();
    auto done = std::make_shared<std::promise<void>>(); auto finished = done->get_future();
    assert(set_auth_interaction_handler([ready, done](AuthInteractionRequest q) {
        ready.wait(); assert(q.message == "SECRET-localized-server-message"); done->set_value();
    }, 100));
    make_owned(false);
    assert(!login({nullptr, nullptr}, 999) && last_auth_outcome() == AuthOutcome::InteractionTimedOut);
    release_owned(); sdk_message.assign(8000, 'x'); // Original SDK objects/strings may now be destroyed.
    gate->set_value(); assert(finished.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    workers_done(); set_auth_interaction_handler({});

    std::ifstream input(log); std::string logs((std::istreambuf_iterator<char>(input)), {});
    assert(logs.find("SECRET") == std::string::npos && logs.find("fixture@example") == std::string::npos);
    assert(logs.find("123456") == std::string::npos && logs.find("987654") == std::string::npos);
    remove(log.c_str()); rmdir(directory);
    puts("SDK UI: native choices/Retry/fields/credentials/codes, ownership and failure/privacy checks passed");
}
