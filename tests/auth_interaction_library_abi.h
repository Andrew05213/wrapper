// Included only by the NDK fixture. Forward declarations let the compiler apply
// the real C++ by-value std::function ABI, instead of hand-marshalling callbacks.
#include <functional>
#include <memory>
#include <thread>
#include <chrono>
namespace storeservicescore { class ProtocolDialog; class ProtocolDialogResponse; class ProtocolTextField; class CredentialsRequest; class CredentialsResponse; }
namespace androidstoreservices {
class AndroidPresentationInterface {
public:
    void showDialog(const std::shared_ptr<storeservicescore::ProtocolDialog>&,
        std::function<void(const std::shared_ptr<storeservicescore::ProtocolDialogResponse>&)>);
    void promptForCredentials(const std::shared_ptr<storeservicescore::CredentialsRequest>&,
        std::function<void(const std::shared_ptr<storeservicescore::CredentialsResponse>&)>);
};
}
extern "C" {
void _ZN17storeservicescore14ProtocolDialogC1Ev(void*);
void _ZN17storeservicescore14ProtocolDialogD2Ev(void*);
void _ZN17storeservicescore14ProtocolDialog15setButtonTitlesEPKcz(void*, const char*, ...);
void _ZN17storeservicescore14ProtocolDialog13setTextFieldsERKNSt6__ndk16vectorINS1_10shared_ptrINS_17ProtocolTextFieldEEENS1_9allocatorIS5_EEEE(void*, void*);
void _ZN17storeservicescore14ProtocolDialog8setTitleERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void*, union std_string*);
void _ZN17storeservicescore14ProtocolDialog10setMessageERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void*, union std_string*);
void _ZN17storeservicescore17ProtocolTextFieldC1Ev(void*);
void _ZN17storeservicescore17ProtocolTextField15setKeyboardTypeENS0_12KeyboardTypeE(void*, int);
void _ZN17storeservicescore17ProtocolTextField9setSecureEb(void*, bool);
void _ZN17storeservicescore17ProtocolTextField8setLabelERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void*, union std_string*);
void _ZN17storeservicescore18CredentialsRequestC1ERKNSt6__ndk110shared_ptrINS_14RequestContextEEEb(void*, struct shared_ptr*, bool);
void _ZN17storeservicescore18CredentialsRequestD2Ev(void*);
int _ZNK17storeservicescore19CredentialsResponse12responseTypeEv(void*);
struct shared_ptr* _ZNK17storeservicescore22ProtocolDialogResponse14selectedButtonEv(void*);
union std_string* _ZNK17storeservicescore19CredentialsResponse8passwordEv(void*);
union std_string* _ZNK17storeservicescore19CredentialsResponse8userNameEv(void*);
}

static void ui_workers_done() {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (auth_interaction_worker_count() && std::chrono::steady_clock::now() < until) std::this_thread::yield();
    assert(!auth_interaction_worker_count());
}
static AuthInteractionResponse ui_response(const AuthInteractionRequest& q, AuthInteractionButtonRole role) {
    AuthInteractionResponse a;
    a.attempt_id = q.attempt_id; a.request_id = q.request_id;
    a.disposition = AuthInteractionDisposition::Submit;
    for (const auto& b : q.buttons) if (b.role == role && b.enabled) { a.button_id = b.id; break; }
    assert(a.button_id);
    return a;
}
static void check_native_ui_retry(void* flow, struct shared_ptr* response) {
    for (bool retry : {true, false}) {
        assert(set_auth_interaction_handler([retry](AuthInteractionRequest q) {
            assert(q.kind == AuthInteractionKind::AuthenticationFailure && q.downstream_attempt_id == 812);
            const auto role = retry ? AuthInteractionButtonRole::Retry : AuthInteractionButtonRole::Cancel;
            assert(submit_auth_interaction_response(ui_response(q, role)) == AuthInteractionSubmitResult::Accepted);
        }, 5000));
        const uint64_t attempt = auth_interaction_begin_attempt(812);
        assert(attempt);
        { std::lock_guard<std::mutex> lock(auth_state_mutex); callback_failed = false; }
        assert(_ZN17storeservicescore16AuthenticateFlow34_shouldRetryAfterIncorrectPasswordERKNSt6__ndk110shared_ptrINS_20AuthenticateResponseEEE(flow, response) == retry);
        const auto result = auth_interaction_end_attempt(attempt);
        assert(result == (retry ? AuthInteractionResult::NotHandled : AuthInteractionResult::Cancelled));
        ui_workers_done(); assert(set_auth_interaction_handler({}));
    }
}

static void check_native_ui(void* flow) {
    auto* presentation = reinterpret_cast<androidstoreservices::AndroidPresentationInterface*>(apInf.obj);
    assert(set_auth_interaction_handler([](AuthInteractionRequest q) {
        assert(q.kind == AuthInteractionKind::AccountChoice);
        AuthInteractionResponse a; a.attempt_id = q.attempt_id; a.request_id = q.request_id;
        a.disposition = AuthInteractionDisposition::Submit; a.button_id = q.buttons[0].id;
        assert(submit_auth_interaction_response(std::move(a)) == AuthInteractionSubmitResult::Accepted);
    }, 5000));
    uint64_t attempt = auth_interaction_begin_attempt(813); assert(attempt);
    assert(_ZN17storeservicescore16AuthenticateFlow20_resolveEmptyAccountEv(flow) == 2);
    assert(auth_interaction_end_attempt(attempt) == AuthInteractionResult::NotHandled);
    ui_workers_done();

    alignas(std::max_align_t) char dialog_storage[144] = {};
    _ZN17storeservicescore14ProtocolDialogC1Ev(dialog_storage);
    library_object_guard dialog_owner(dialog_storage, _ZN17storeservicescore14ProtocolDialogD2Ev);
    _ZN17storeservicescore14ProtocolDialog14setFailureTypeEx(dialog_storage, 0);
    _ZN17storeservicescore14ProtocolDialog15setButtonTitlesEPKcz(dialog_storage, "明确继续", "取消", nullptr);
    auto title = new_std_string("离线原生提示"), message = new_std_string("SECRET-native-ui-message");
    _ZN17storeservicescore14ProtocolDialog8setTitleERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(dialog_storage, &title);
    _ZN17storeservicescore14ProtocolDialog10setMessageERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(dialog_storage, &message);
    auto dialog = std::shared_ptr<storeservicescore::ProtocolDialog>(reinterpret_cast<storeservicescore::ProtocolDialog*>(dialog_storage), [](storeservicescore::ProtocolDialog*){});
    auto make_field = [](bool secure) {
        auto* storage = static_cast<char*>(::operator new(64));
        _ZN17storeservicescore17ProtocolTextFieldC1Ev(storage);
        _ZN17storeservicescore17ProtocolTextField9setSecureEb(storage, secure);
        auto label = new_std_string(secure ? "秘密输入" : "普通输入");
        _ZN17storeservicescore17ProtocolTextField8setLabelERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(storage, &label);
        return std::shared_ptr<storeservicescore::ProtocolTextField>(reinterpret_cast<storeservicescore::ProtocolTextField*>(storage), [](storeservicescore::ProtocolTextField* p) {
            auto* data = reinterpret_cast<char*>(p);
            reinterpret_cast<std::string*>(data + 8)->~basic_string();
            reinterpret_cast<std::string*>(data + 40)->~basic_string();
            ::operator delete(data);
        });
    };
    std::vector<std::shared_ptr<storeservicescore::ProtocolTextField>> fields{make_field(false), make_field(true)};
    _ZN17storeservicescore14ProtocolDialog13setTextFieldsERKNSt6__ndk16vectorINS1_10shared_ptrINS_17ProtocolTextFieldEEENS1_9allocatorIS5_EEEE(dialog_storage, &fields);
    assert(set_auth_interaction_handler([](AuthInteractionRequest q) {
        assert(q.kind == AuthInteractionKind::ProtocolDialog && q.title == "离线原生提示");
        assert(q.fields.size() == 2 && q.fields[0].type == AuthInteractionFieldType::Text && q.fields[1].type == AuthInteractionFieldType::Secret);
        auto a = ui_response(q, AuthInteractionButtonRole::Choice);
        for (const auto& f : q.fields) {
            AuthInteractionValue value; value.field_id = f.id;
            value.value = f.type == AuthInteractionFieldType::Secret ? "SECRET-native-secret" : "SECRET-native-text";
            a.values.push_back(std::move(value));
        }
        assert(submit_auth_interaction_response(std::move(a)) == AuthInteractionSubmitResult::Accepted);
    }, 5000));
    attempt = auth_interaction_begin_attempt(814); assert(attempt);
    int dialog_completed = 0;
    presentation->showDialog(dialog, [&](const std::shared_ptr<storeservicescore::ProtocolDialogResponse>& r) {
        ++dialog_completed;
        for (size_t i = 0; i < fields.size(); ++i) {
            struct shared_ptr field; memcpy(&field, &fields[i], sizeof(field));
            std::string actual;
            _ZN17storeservicescore22ProtocolDialogResponse17valueForTextFieldERKNSt6__ndk110shared_ptrINS_17ProtocolTextFieldEEE(&actual, r.get(), &field);
            const std::string expected = i ? "SECRET-native-secret" : "SECRET-native-text";
            assert(actual == expected);
        }
    });
    assert(dialog_completed == 1 && dialog.use_count() == 1);
    assert(auth_interaction_end_attempt(attempt) == AuthInteractionResult::NotHandled);
    ui_workers_done();

    _ZN17storeservicescore17ProtocolTextField15setKeyboardTypeENS0_12KeyboardTypeE(fields[0].get(), 999);
    assert(set_auth_interaction_handler([](AuthInteractionRequest q) {
        assert(q.kind == AuthInteractionKind::Unsupported && q.unsupported == AuthInteractionUnsupported::FieldType);
        assert(!q.title.empty() && !q.message.empty());
        AuthInteractionResponse a; a.attempt_id = q.attempt_id; a.request_id = q.request_id;
        a.disposition = AuthInteractionDisposition::Unsupported;
        assert(submit_auth_interaction_response(std::move(a)) == AuthInteractionSubmitResult::Accepted);
    }, 5000));
    attempt = auth_interaction_begin_attempt(816); assert(attempt);
    dialog_completed = 0;
    presentation->showDialog(dialog, [&](const std::shared_ptr<storeservicescore::ProtocolDialogResponse>& r) {
        ++dialog_completed;
        assert(!_ZNK17storeservicescore22ProtocolDialogResponse14selectedButtonEv(r.get())->obj);
    });
    assert(dialog_completed == 1);
    assert(auth_interaction_end_attempt(attempt) == AuthInteractionResult::Unsupported);
    ui_workers_done();

    int challenges = 0;
    assert(set_auth_interaction_handler([&](AuthInteractionRequest q) {
        if (q.kind == AuthInteractionKind::Unsupported)
            printf("NATIVE_UI_UNSUPPORTED reason=%u fields=%zu\n", static_cast<unsigned>(q.unsupported), q.fields.size());
        assert(q.kind == AuthInteractionKind::Credentials || q.kind == AuthInteractionKind::VerificationCode);
        auto a = ui_response(q, AuthInteractionButtonRole::Submit);
        if (q.kind == AuthInteractionKind::VerificationCode) ++challenges;
        for (const auto& f : q.fields) {
            AuthInteractionValue v; v.field_id = f.id;
            if (f.type == AuthInteractionFieldType::Account) v.value = "fixture@example.invalid";
            else if (f.type == AuthInteractionFieldType::Secret) v.value = " SECRET-native-original ";
            else { assert(f.type == AuthInteractionFieldType::VerificationCode); v.value = challenges == 1 ? "123456" : "987654"; }
            a.values.push_back(std::move(v));
        }
        assert(submit_auth_interaction_response(std::move(a)) == AuthInteractionSubmitResult::Accepted);
    }, 5000));
    attempt = auth_interaction_begin_attempt(815); assert(attempt);
    int credentials_completed = 0;
    for (int step = 0; step < 3; ++step) {
        alignas(std::max_align_t) char request_storage[512] = {};
        _ZN17storeservicescore18CredentialsRequestC1ERKNSt6__ndk110shared_ptrINS_14RequestContextEEEb(request_storage, &g_reqCtx, step != 0);
        library_object_guard owner(request_storage, _ZN17storeservicescore18CredentialsRequestD2Ev);
        auto request = std::shared_ptr<storeservicescore::CredentialsRequest>(reinterpret_cast<storeservicescore::CredentialsRequest*>(request_storage), [](storeservicescore::CredentialsRequest*){});
        presentation->promptForCredentials(request, [&](const std::shared_ptr<storeservicescore::CredentialsResponse>& response) {
            ++credentials_completed;
            assert(_ZNK17storeservicescore19CredentialsResponse12responseTypeEv(response.get()) == 2);
            const std::string user = std_string_data(_ZNK17storeservicescore19CredentialsResponse8userNameEv(response.get()));
            const std::string password = std_string_data(_ZNK17storeservicescore19CredentialsResponse8passwordEv(response.get()));
            const std::string expected_user = "fixture@example.invalid";
            const std::string expected_password = std::string(" SECRET-native-original ") + (step == 0 ? "" : step == 1 ? "123456" : "987654");
            assert(user == expected_user && password == expected_password);
        });
        assert(request.use_count() == 1);
        ui_workers_done();
    }
    assert(credentials_completed == 3 && challenges == 2);
    assert(auth_interaction_end_attempt(attempt) == AuthInteractionResult::NotHandled);
    assert(set_auth_interaction_handler({}));
    std::string user, password;
    assert(!auth_interaction_get_credentials(user, password));
    puts("NATIVE_UI_ABI_PASSED");
}
