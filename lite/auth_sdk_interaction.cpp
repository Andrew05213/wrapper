#include "internal.h"
#include "library_ownership.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace {
using Kind = AuthInteractionKind;
using Role = AuthInteractionButtonRole;
using Type = AuthInteractionFieldType;
using Unsupported = AuthInteractionUnsupported;
using Result = AuthInteractionResult;

struct Secrets {
    std::string user, password;
    ~Secrets() { auth_interaction_wipe(user); auth_interaction_wipe(password); }
};

void unsupported(AuthInteractionRequest& q, Unsupported reason) {
    q.kind = Kind::Unsupported;
    if (q.unsupported == Unsupported::None) q.unsupported = reason;
}

void text(AuthInteractionRequest& q, std::string& out, union std_string* s, size_t maximum) {
    if (!s) { unsupported(q, Unsupported::Metadata); return; }
    const size_t size = (s->mark & 1) ? s->size : s->mark >> 1;
    const char* data = std_string_data(s);
    if (!data && size) { unsupported(q, Unsupported::Metadata); return; }
    if (size > maximum) { unsupported(q, Unsupported::Limits); q.text_truncated = true; }
    out.assign(data ? data : "", std::min(size, maximum));
    const auto nul = out.find('\0');
    if (nul != std::string::npos) { out.resize(nul); unsupported(q, Unsupported::Metadata); q.text_truncated = true; }
    // A bounded prefix may split one UTF-8 character. Repair only that boundary;
    // genuinely invalid UTF-8 is not forwarded as actionable metadata.
    if (size > maximum)
        for (int i = 0; i < 3 && !out.empty() && !auth_interaction_utf8(out); ++i) out.pop_back();
    if (!auth_interaction_utf8(out)) { out.clear(); unsupported(q, Unsupported::Metadata); q.text_truncated = true; }
}

size_t count(AuthInteractionRequest& q, const struct std_vector* v, size_t limit) {
    if (!v) { unsupported(q, Unsupported::Metadata); return 0; }
    const auto begin = reinterpret_cast<uintptr_t>(v->begin), end = reinterpret_cast<uintptr_t>(v->end);
    if ((!begin && end) || end < begin || (end - begin) % sizeof(struct shared_ptr)) {
        unsupported(q, Unsupported::Metadata); return 0;
    }
    const size_t n = (end - begin) / sizeof(struct shared_ptr);
    if (n > limit) unsupported(q, Unsupported::Limits);
    return std::min(n, limit);
}

bool safe_action_value(struct shared_ptr* action) {
    if (!action || !action->obj) return true;
    using Getter = union std_string* (*)(void*);
    auto getter = reinterpret_cast<Getter>((*static_cast<void***>(action->obj))[0]);
    auto* type = getter(action->obj);
    if (!type) return false;
    const size_t size = (type->mark & 1) ? type->size : type->mark >> 1;
    return size == 12 && memcmp(std_string_data(type), "authenticate", 12) == 0;
}
bool safe_action(void* button) {
    return safe_action_value(_ZNK17storeservicescore14ProtocolButton6actionEv(button));
}

void read_fields(AuthInteractionRequest& q, const struct std_vector* vector) {
    const size_t n = count(q, vector, AuthInteractionLimits::Fields);
    auto* list = vector ? static_cast<struct shared_ptr*>(vector->begin) : nullptr;
    for (size_t i = 0; i < n; ++i) {
        AuthInteractionField f;
        f.id = auth_interaction_item_id();
        if (!list[i].obj) unsupported(q, Unsupported::Metadata);
        else {
            for (size_t j = 0; j < i; ++j) if (list[i].obj == list[j].obj) unsupported(q, Unsupported::Metadata);
            text(q, f.label, _ZNK17storeservicescore17ProtocolTextField5labelEv(list[i].obj), AuthInteractionLimits::Label);
            const int keyboard = _ZNK17storeservicescore17ProtocolTextField12keyboardTypeEv(list[i].obj);
            const bool secure = _ZNK17storeservicescore17ProtocolTextField8isSecureEv(list[i].obj);
            // Verified native table: 0 -> default, 1 -> email, 2 -> number pad.
            // No label inference and no call to the SDK's unchecked table getter.
            if (keyboard < 0 || keyboard > 2) unsupported(q, Unsupported::FieldType);
            else {
                f.type = secure ? Type::Secret : (keyboard == 1 ? Type::Email : (keyboard == 2 ? Type::Digits : Type::Text));
                f.digits_only = keyboard == 2;
                if (f.type == Type::Email) f.max_bytes = AuthInteractionLimits::Account;
            }
        }
        q.fields.push_back(std::move(f));
    }
}

void finalize(AuthInteractionRequest& q) {
    size_t other = q.title.size();
    for (const auto& b : q.buttons) other += b.label.size();
    for (const auto& f : q.fields) other += f.label.size();
    if (other + q.message.size() > AuthInteractionLimits::DisplayTotal) {
        unsupported(q, Unsupported::Limits); q.text_truncated = true;
        q.message.resize(other < AuthInteractionLimits::DisplayTotal ? AuthInteractionLimits::DisplayTotal - other : 0);
        while (!auth_interaction_utf8(q.message)) q.message.pop_back();
    }
    if (q.kind == Kind::Unsupported) for (auto& b : q.buttons) { b.enabled = false; b.unsupported = q.unsupported; }
}

void allocate_response(library_shared_ptr& owner, size_t size, void* vtable, void (*construct)(void*)) {
    auto* storage = static_cast<unsigned char*>(calloc(1, size));
    if (!storage) throw std::bad_alloc();
    *reinterpret_cast<void**>(storage) = vtable;
    owner.value = {storage + 24, storage};
    construct(owner.value.obj);
}
void complete_dialog(long id, struct shared_ptr* handler, struct shared_ptr* response) {
    if (handler && handler->obj) {
        // SDK passes the AndroidProtocolDialogResponseHandler secondary base;
        // vtable slot 2 is its virtual completion, after the two destructors.
        using Complete = void (*)(void*, long*, struct shared_ptr*);
        auto fn = reinterpret_cast<Complete>((*static_cast<void***>(handler->obj))[2]);
        fn(handler->obj, &id, response);
    } else {
        _ZN20androidstoreservices28AndroidPresentationInterface28handleProtocolDialogResponseERKlRKNSt6__ndk110shared_ptrIN17storeservicescore22ProtocolDialogResponseEEE(apInf.obj, &id, response);
    }
}
void complete_credentials(struct shared_ptr* handler, struct shared_ptr* response) {
    if (handler && handler->obj) {
        using Complete = void (*)(void*, struct shared_ptr*);
        auto fn = reinterpret_cast<Complete>((*static_cast<void***>(handler->obj))[2]);
        fn(handler->obj, response);
    } else {
        _ZN20androidstoreservices28AndroidPresentationInterface25handleCredentialsResponseERKNSt6__ndk110shared_ptrIN17storeservicescore19CredentialsResponseEEE(apInf.obj, response);
    }
}

const std::string& value(const AuthInteractionResponse& answer, uint64_t id) {
    for (const auto& v : answer.values) if (v.field_id == id) return v.value;
    throw std::runtime_error("auth_interaction_missing_field");
}
} // namespace

bool auth_handle_dialog_interaction(long id, struct shared_ptr* dialog, struct shared_ptr* handler) {
    library_shared_ptr request_owner, handler_owner, response;
    request_owner.retain(dialog); handler_owner.retain(handler);
    allocate_response(response, 72,
        &_ZTVNSt6__ndk120__shared_ptr_emplaceIN17storeservicescore22ProtocolDialogResponseENS_9allocatorIS2_EEEE + 2,
        _ZN17storeservicescore22ProtocolDialogResponseC1Ev);
    _ZN17storeservicescore22ProtocolDialogResponse31setPerformsDefaultButtonActionsEb(response.value.obj, false);
    const auto status = auth_interaction_status();
    if (status != Result::NotHandled) {
        auth_note_interaction_failure(status);
        complete_dialog(id, &handler_owner.value, &response.value);
        return true;
    }
    try {
        AuthInteractionRequest q;
        q.kind = Kind::ProtocolDialog;
        AuthNumber kind, failure;
        const auto origin = auth_dialog_origin();
        struct std_vector* buttons = nullptr;
        struct std_vector* inputs = nullptr;
        if (!dialog || !dialog->obj) unsupported(q, Unsupported::Metadata);
        else {
            if (origin != AuthDialogOrigin::Protocol)
                _ZN17storeservicescore14ProtocolDialog14setFailureTypeEx(dialog->obj, 0);
            kind.set(_ZNK17storeservicescore14ProtocolDialog10dialogKindEv(dialog->obj));
            failure.set(_ZNK17storeservicescore14ProtocolDialog11failureTypeEv(dialog->obj));
            text(q, q.title, _ZNK17storeservicescore14ProtocolDialog5titleEv(dialog->obj), AuthInteractionLimits::Title);
            text(q, q.message, _ZNK17storeservicescore14ProtocolDialog7messageEv(dialog->obj), AuthInteractionLimits::Message);
            buttons = _ZNK17storeservicescore14ProtocolDialog7buttonsEv(dialog->obj);
            inputs = _ZNK17storeservicescore14ProtocolDialog10textFieldsEv(dialog->obj);
            if (origin == AuthDialogOrigin::Protocol && kind.value != 0) unsupported(q, Unsupported::Metadata);
            if (_ZNK17storeservicescore14ProtocolDialog29allowsBiometricAuthenticationEv(dialog->obj))
                unsupported(q, Unsupported::FieldType);
            const size_t n = count(q, buttons, AuthInteractionLimits::Buttons);
            auto* list = buttons ? static_cast<struct shared_ptr*>(buttons->begin) : nullptr;
            if (!n) unsupported(q, Unsupported::Metadata);
            for (size_t i = 0; i < n; ++i) {
                AuthInteractionButton b; b.id = auth_interaction_item_id();
                if (!list[i].obj) unsupported(q, Unsupported::Metadata);
                else {
                    text(q, b.label, _ZNK17storeservicescore14ProtocolButton5titleEv(list[i].obj), AuthInteractionLimits::Label);
                    if (!safe_action(list[i].obj)) unsupported(q, Unsupported::Action);
                }
                q.buttons.push_back(std::move(b));
            }
            read_fields(q, inputs);
            if (origin == AuthDialogOrigin::AccountChoice) {
                if (kind.value != 2 || failure.value || q.buttons.size() != 3 || !q.fields.empty()) unsupported(q, Unsupported::Metadata);
                if (q.kind != Kind::Unsupported) {
                    q.kind = Kind::AccountChoice;
                    q.buttons[1].enabled = false; // Native account creation needs external UI.
                    q.buttons[1].unsupported = Unsupported::Action;
                    q.buttons[2].role = Role::Cancel;
                }
                auth_notify_interaction_event(AuthEventType::AccountChoice, kind, failure);
            } else if (origin == AuthDialogOrigin::FailurePrompt) {
                if (kind.value != 0 || failure.value || q.buttons.size() != 2 || !q.fields.empty()) unsupported(q, Unsupported::Metadata);
                if (q.kind != Kind::Unsupported) {
                    q.kind = Kind::AuthenticationFailure;
                    q.buttons[0].role = Role::Cancel; q.buttons[1].role = Role::Retry;
                }
                auth_notify_interaction_event(AuthEventType::AuthenticationFailurePrompt, kind, failure);
            }
        }
        finalize(q);
        if (q.kind == Kind::Unsupported) auth_notify_interaction_event(AuthEventType::UnsupportedDialog, kind, failure);
        // Keep only the ID mapping/schema on this SDK thread; the worker receives
        // copied UI text and never captures these SDK vectors or owners.
        const auto offered_buttons = q.buttons;
        const auto offered_fields = q.fields;
        AuthInteractionResponse answer;
        Result result = auth_interaction_ask(std::move(q), answer);
        if (result == Result::NotHandled) result = Result::Unavailable;
        auth_note_interaction_failure(result);
        if (result == Result::Answered) {
            size_t selected = offered_buttons.size();
            for (size_t i = 0; i < offered_buttons.size(); ++i) if (offered_buttons[i].id == answer.button_id) selected = i;
            if (selected == offered_buttons.size()) throw std::runtime_error("auth_interaction_invalid_button");
            auto* list = static_cast<struct shared_ptr*>(buttons->begin);
            if (offered_buttons[selected].role == Role::Cancel) {
                cancel_auth_interaction(answer.attempt_id); auth_note_interaction_failure(Result::Cancelled);
            } else {
                auto* fields_list = inputs ? static_cast<struct shared_ptr*>(inputs->begin) : nullptr;
                for (size_t i = 0; i < offered_fields.size(); ++i) {
                    auto view = new_std_string(value(answer, offered_fields[i].id).c_str());
                    _ZN17storeservicescore22ProtocolDialogResponse17setTextFieldValueERKNSt6__ndk110shared_ptrINS_17ProtocolTextFieldEEERKNS1_12basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(response.value.obj, fields_list + i, &view);
                }
            }
            _ZN17storeservicescore22ProtocolDialogResponse17setSelectedButtonERKNSt6__ndk110shared_ptrINS_14ProtocolButtonEEE(response.value.obj, list + selected);
            if (origin == AuthDialogOrigin::AccountChoice && selected == 0)
                auth_notify_interaction_event(AuthEventType::ExistingAccountSelected, kind, failure);
        }
    } catch (...) { auth_note_interaction_failure(Result::HandlerFailed); }
    // A single completion call, on the SDK callback thread, even on every error.
    complete_dialog(id, &handler_owner.value, &response.value);
    return true;
}

bool auth_handle_credentials_interaction(struct shared_ptr* request, struct shared_ptr* handler) {
    library_shared_ptr request_owner, handler_owner, response;
    request_owner.retain(request); handler_owner.retain(handler);
    allocate_response(response, 80,
        &_ZTVNSt6__ndk120__shared_ptr_emplaceIN17storeservicescore19CredentialsResponseENS_9allocatorIS2_EEEE + 2,
        _ZN17storeservicescore19CredentialsResponseC1Ev);
    _ZN17storeservicescore19CredentialsResponse15setResponseTypeENS0_12ResponseTypeE(response.value.obj, 0);
    const auto status = auth_interaction_status();
    if (status != Result::NotHandled) {
        auth_note_interaction_failure(status);
        complete_credentials(&handler_owner.value, &response.value);
        return true;
    }
    auth_notify_interaction_event(AuthEventType::CredentialsRequested);
    try {
        AuthInteractionRequest q;
        q.kind = Kind::Credentials;
        const bool valid = request && request->obj;
        const bool code = valid && _ZNK17storeservicescore18CredentialsRequest28requiresHSA2VerificationCodeEv(request->obj);
        Secrets original;
        library_shared_ptr generated_dialog;
        if (!valid) unsupported(q, Unsupported::Metadata);
        else {
            text(q, q.title, _ZNK17storeservicescore18CredentialsRequest5titleEv(request->obj), AuthInteractionLimits::Title);
            text(q, q.message, _ZNK17storeservicescore18CredentialsRequest7messageEv(request->obj), AuthInteractionLimits::Message);
            if (!safe_action_value(_ZNK17storeservicescore18CredentialsRequest14okButtonActionEv(request->obj)))
                unsupported(q, Unsupported::Action);
            _ZNK17storeservicescore18CredentialsRequest6dialogEv(&generated_dialog.value, request->obj);
            struct std_vector* native_buttons = nullptr;
            if (!generated_dialog.value.obj) unsupported(q, Unsupported::Metadata);
            else {
                if (q.title.empty()) text(q, q.title, _ZNK17storeservicescore14ProtocolDialog5titleEv(generated_dialog.value.obj), AuthInteractionLimits::Title);
                if (q.message.empty()) text(q, q.message, _ZNK17storeservicescore14ProtocolDialog7messageEv(generated_dialog.value.obj), AuthInteractionLimits::Message);
                native_buttons = _ZNK17storeservicescore14ProtocolDialog7buttonsEv(generated_dialog.value.obj);
                if (count(q, native_buttons, AuthInteractionLimits::Buttons) != 2) unsupported(q, Unsupported::Metadata);
            }
            AuthInteractionButton ok, cancel;
            ok.id = auth_interaction_item_id(); ok.role = Role::Submit;
            cancel.id = auth_interaction_item_id(); cancel.role = Role::Cancel;
            text(q, ok.label, _ZNK17storeservicescore18CredentialsRequest13okButtonTitleEv(request->obj), AuthInteractionLimits::Label);
            text(q, cancel.label, _ZNK17storeservicescore18CredentialsRequest17cancelButtonTitleEv(request->obj), AuthInteractionLimits::Label);
            if (native_buttons && count(q, native_buttons, AuthInteractionLimits::Buttons) == 2) {
                auto* list = static_cast<struct shared_ptr*>(native_buttons->begin);
                if (!list[0].obj || !list[1].obj) unsupported(q, Unsupported::Metadata);
                else {
                    if (ok.label.empty()) text(q, ok.label, _ZNK17storeservicescore14ProtocolButton5titleEv(list[1].obj), AuthInteractionLimits::Label);
                    if (cancel.label.empty()) text(q, cancel.label, _ZNK17storeservicescore14ProtocolButton5titleEv(list[0].obj), AuthInteractionLimits::Label);
                }
            }
            q.buttons.push_back(std::move(ok)); q.buttons.push_back(std::move(cancel));
            if (code) {
                if (!auth_interaction_get_credentials(original.user, original.password)) {
                    original.user = amUsername ? amUsername : ""; original.password = amPassword ? amPassword : "";
                }
                if (original.user.empty() || original.password.empty() || original.user.size() > AuthInteractionLimits::Account ||
                    original.password.size() > AuthInteractionLimits::Input || !auth_interaction_utf8(original.user) ||
                    !auth_interaction_utf8(original.password)) unsupported(q, Unsupported::MissingCredentials);
                if (q.kind != Kind::Unsupported) q.kind = Kind::VerificationCode;
                AuthInteractionField field; field.id = auth_interaction_item_id();
                field.label = "Verification code"; field.type = Type::VerificationCode;
                field.min_bytes = field.max_bytes = AuthInteractionLimits::Code; field.digits_only = true;
                q.fields.push_back(std::move(field));
                auth_notify_interaction_event(AuthEventType::VerificationCodeRequested);
            } else {
                if (!generated_dialog.value.obj) unsupported(q, Unsupported::Metadata);
                else read_fields(q, _ZNK17storeservicescore14ProtocolDialog10textFieldsEv(generated_dialog.value.obj));
                if (q.fields.size() != 2 || q.fields[0].type != Type::Email || q.fields[1].type != Type::Secret)
                    unsupported(q, Unsupported::FieldType);
                else q.fields[0].type = Type::Account;
            }
        }
        finalize(q);
        if (q.kind == Kind::Unsupported) auth_notify_interaction_event(AuthEventType::UnsupportedDialog);
        const auto offered_buttons = q.buttons;
        const auto offered_fields = q.fields;
        AuthInteractionResponse answer;
        Result result = auth_interaction_ask(std::move(q), answer);
        if (result == Result::NotHandled) result = Result::Unavailable;
        auth_note_interaction_failure(result);
        if (result == Result::Answered) {
            const AuthInteractionButton* selected = nullptr;
            for (const auto& b : offered_buttons) if (b.id == answer.button_id) selected = &b;
            if (!selected) throw std::runtime_error("auth_interaction_invalid_button");
            if (selected->role == Role::Cancel) {
                cancel_auth_interaction(answer.attempt_id); auth_note_interaction_failure(Result::Cancelled);
            } else {
                if (code) original.password += value(answer, offered_fields[0].id);
                else {
                    original.user = value(answer, offered_fields[0].id);
                    original.password = value(answer, offered_fields[1].id);
                    auth_interaction_set_credentials(original.user, original.password);
                }
                auto user = new_std_string(original.user.c_str()), password = new_std_string(original.password.c_str());
                _ZN17storeservicescore19CredentialsResponse11setUserNameERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(response.value.obj, &user);
                _ZN17storeservicescore19CredentialsResponse11setPasswordERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(response.value.obj, &password);
                _ZN17storeservicescore19CredentialsResponse15setResponseTypeENS0_12ResponseTypeE(response.value.obj, 2);
            }
        }
    } catch (...) { auth_note_interaction_failure(Result::HandlerFailed); }
    complete_credentials(&handler_owner.value, &response.value);
    return true;
}
