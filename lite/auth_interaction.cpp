#include "auth_interaction_internal.h"
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <limits>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
struct Pending {
    AuthInteractionRequest schema;
    AuthInteractionResponse response;
    Clock::time_point deadline;
    AuthInteractionResult result = AuthInteractionResult::NotHandled; // waiting
    bool handler_returned = false;
};
struct Registration {
    AuthInteractionHandler handler;
    uint32_t timeout;
    Registration(AuthInteractionHandler h, uint32_t t) : handler(std::move(h)), timeout(t) {}
};
struct Runtime {
    std::mutex mutex;
    std::condition_variable changed;
    std::shared_ptr<Registration> registration;
    std::shared_ptr<Pending> pending;
    uint64_t next = 1, attempt = 0, downstream = 0;
    bool owned = false;
    size_t workers = 0;
    AuthInteractionResult terminal = AuthInteractionResult::NotHandled;
    std::string user, password;
};
Runtime& runtime() {
    // Bounded process-lifetime state. Detached user code may still be returning
    // during shutdown; never destroy the mutex underneath an in-flight worker.
    static Runtime* state = new Runtime;
    return *state;
}
uint64_t next_id(Runtime& r) {
    if (!r.next || r.next == std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("auth_interaction_id_exhausted");
    return r.next++;
}
void retire(Runtime& r, AuthInteractionResult why) {
    if (r.terminal == AuthInteractionResult::NotHandled) r.terminal = why;
    if (r.pending && r.pending->result == AuthInteractionResult::NotHandled)
        r.pending->result = r.terminal;
    r.changed.notify_all();
}
bool valid_schema(const AuthInteractionRequest& q) {
    using namespace AuthInteractionLimits;
    if (q.protocol_version != Version || static_cast<uint32_t>(q.kind) < 1 || static_cast<uint32_t>(q.kind) > 6 ||
        q.title.size() > Title || q.message.size() > Message ||
        q.buttons.size() > Buttons || q.fields.size() > Fields ||
        !auth_interaction_utf8(q.title) || !auth_interaction_utf8(q.message)) return false;
    size_t total = q.title.size() + q.message.size();
    for (size_t i = 0; i < q.buttons.size(); ++i) {
        const auto& b = q.buttons[i];
        if (!b.id || b.label.size() > Label || !auth_interaction_utf8(b.label) ||
            static_cast<uint32_t>(b.role) < 1 || static_cast<uint32_t>(b.role) > 4) return false;
        for (size_t j = 0; j < i; ++j) if (q.buttons[j].id == b.id) return false;
        total += b.label.size();
    }
    for (size_t i = 0; i < q.fields.size(); ++i) {
        const auto& f = q.fields[i];
        if (!f.id || f.label.size() > Label || !auth_interaction_utf8(f.label) ||
            f.max_bytes > Input || f.min_bytes > f.max_bytes ||
            static_cast<uint32_t>(f.type) < 1 || static_cast<uint32_t>(f.type) > 7 ||
            (f.type == AuthInteractionFieldType::Unsupported && q.kind != AuthInteractionKind::Unsupported)) return false;
        for (size_t j = 0; j < i; ++j) if (q.fields[j].id == f.id) return false;
        total += f.label.size();
    }
    return total <= DisplayTotal;
}
void invoke(std::shared_ptr<Registration> registration, AuthInteractionRequest request) {
    auto& r = runtime();
    const uint64_t id = request.request_id;
    bool current;
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        current = r.pending && r.pending->schema.request_id == id &&
                  r.pending->result == AuthInteractionResult::NotHandled;
        if (current) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(r.pending->deadline - Clock::now()).count();
            request.expires_after_ms = remaining > 0 ? static_cast<uint32_t>(remaining) : 0;
            if (!request.expires_after_ms) { retire(r, AuthInteractionResult::TimedOut); current = false; }
        }
    }
    try {
        if (current) registration->handler(std::move(request));
    } catch (...) {
        std::lock_guard<std::mutex> lock(r.mutex);
        if (r.pending && r.pending->schema.request_id == id)
            retire(r, AuthInteractionResult::HandlerFailed);
    }
    registration.reset(); // Arbitrary capture destructors still count against the worker budget.
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        if (r.pending && r.pending->schema.request_id == id) {
            if (Clock::now() >= r.pending->deadline) retire(r, AuthInteractionResult::TimedOut);
            r.pending->handler_returned = true;
        }
        --r.workers; r.changed.notify_all();
    }
    // Captured user code and UI values are destroyed after unlocking.
}
} // namespace

bool auth_interaction_utf8(const std::string& text) {
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i++]);
        if (!c) return false;
        if (c < 0x80) continue;
        uint32_t n, value, minimum;
        if ((c & 0xe0) == 0xc0) { n = 1; value = c & 31; minimum = 0x80; }
        else if ((c & 0xf0) == 0xe0) { n = 2; value = c & 15; minimum = 0x800; }
        else if ((c & 0xf8) == 0xf0) { n = 3; value = c & 7; minimum = 0x10000; }
        else return false;
        if (i + n > text.size()) return false;
        while (n--) {
            const unsigned char next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

bool set_auth_interaction_handler(AuthInteractionHandler handler, uint32_t timeout_ms) {
    if (timeout_ms < AuthInteractionLimits::MinTimeoutMs || timeout_ms > AuthInteractionLimits::MaxTimeoutMs)
        return false;
    std::shared_ptr<Registration> replacement;
    if (handler) replacement = std::make_shared<Registration>(std::move(handler), timeout_ms);
    auto& r = runtime();
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        r.registration.swap(replacement);
        if (r.attempt && r.owned) retire(r, AuthInteractionResult::Unavailable);
        else if (r.attempt && r.registration) r.owned = true;
    }
    // The previous handler's captures can run arbitrary destructors; no lock.
    return true;
}

uint64_t auth_interaction_begin_attempt(uint64_t downstream_attempt_id) {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (r.attempt) return 0;
    r.attempt = next_id(r); r.downstream = downstream_attempt_id;
    r.owned = static_cast<bool>(r.registration);
    r.terminal = AuthInteractionResult::NotHandled;
    return r.attempt;
}
AuthInteractionResult auth_interaction_end_attempt(uint64_t attempt_id) {
    auto& r = runtime();
    std::shared_ptr<Pending> old;
    AuthInteractionResult result;
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        if (!attempt_id || r.attempt != attempt_id) return AuthInteractionResult::Unavailable;
        result = r.terminal;
        if (r.pending) retire(r, AuthInteractionResult::Cancelled);
        old.swap(r.pending);
        auth_interaction_wipe(r.user); auth_interaction_wipe(r.password);
        r.attempt = 0; r.downstream = 0; r.owned = false;
    }
    return result;
}
bool auth_interaction_handles_attempt() {
    auto& r = runtime(); std::lock_guard<std::mutex> lock(r.mutex);
    return r.attempt && (r.owned || r.registration || r.terminal != AuthInteractionResult::NotHandled);
}
uint64_t auth_interaction_item_id() {
    auto& r = runtime(); std::lock_guard<std::mutex> lock(r.mutex); return next_id(r);
}
size_t auth_interaction_worker_count() {
    auto& r = runtime(); std::lock_guard<std::mutex> lock(r.mutex); return r.workers;
}
bool cancel_auth_interaction(uint64_t attempt_id) {
    auto& r = runtime(); std::lock_guard<std::mutex> lock(r.mutex);
    if (!attempt_id || r.attempt != attempt_id) return false;
    retire(r, AuthInteractionResult::Cancelled); return true;
}
uint64_t current_auth_interaction_attempt() {
    auto& r = runtime(); std::lock_guard<std::mutex> lock(r.mutex); return r.attempt;
}
void auth_interaction_retire(AuthInteractionResult result) {
    if (result == AuthInteractionResult::NotHandled || result == AuthInteractionResult::Answered) return;
    auto& r = runtime(); std::lock_guard<std::mutex> lock(r.mutex);
    if (r.attempt) retire(r, result);
}
AuthInteractionResult auth_interaction_status() {
    auto& r = runtime(); std::lock_guard<std::mutex> lock(r.mutex);
    return r.attempt ? r.terminal : AuthInteractionResult::Unavailable;
}
bool auth_interaction_get_credentials(std::string& user, std::string& password) {
    auto& r = runtime(); std::lock_guard<std::mutex> lock(r.mutex);
    if (r.user.empty() || r.password.empty()) return false;
    user = r.user; password = r.password; return true;
}
void auth_interaction_set_credentials(const std::string& user, const std::string& password) {
    auto& r = runtime(); std::lock_guard<std::mutex> lock(r.mutex);
    auth_interaction_wipe(r.user); auth_interaction_wipe(r.password);
    r.user = user; r.password = password;
}

AuthInteractionResult auth_interaction_ask(AuthInteractionRequest request, AuthInteractionResponse& answer) {
    auto& r = runtime();
    auto pending = std::make_shared<Pending>();
    std::shared_ptr<Registration> registration;
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        if (!r.attempt) return AuthInteractionResult::Unavailable;
        if (r.terminal != AuthInteractionResult::NotHandled) return r.terminal;
        if (!r.registration) return r.owned ? AuthInteractionResult::Unavailable : AuthInteractionResult::NotHandled;
        r.owned = true;
        if (r.pending || r.workers >= AuthInteractionLimits::Workers) {
            retire(r, AuthInteractionResult::Unavailable); return r.terminal;
        }
        if (!valid_schema(request)) { retire(r, AuthInteractionResult::Unsupported); return r.terminal; }
        registration = r.registration;
        request.attempt_id = r.attempt; request.downstream_attempt_id = r.downstream;
        request.request_id = next_id(r);
        pending->schema = request;
        // Validation needs only IDs/constraints, not a second copy of UI text.
        auth_interaction_wipe(pending->schema.title); auth_interaction_wipe(pending->schema.message);
        for (auto& b : pending->schema.buttons) auth_interaction_wipe(b.label);
        for (auto& f : pending->schema.fields) auth_interaction_wipe(f.label);
        pending->deadline = Clock::now() + std::chrono::milliseconds(registration->timeout);
        r.pending = pending; ++r.workers;
    }
    try { std::thread(invoke, registration, std::move(request)).detach(); }
    catch (...) {
        std::lock_guard<std::mutex> lock(r.mutex);
        --r.workers; retire(r, AuthInteractionResult::Unavailable);
    }
    AuthInteractionResult result;
    {
        std::unique_lock<std::mutex> lock(r.mutex);
        while (r.terminal == AuthInteractionResult::NotHandled &&
               (pending->result == AuthInteractionResult::NotHandled ||
                (pending->result == AuthInteractionResult::Answered && !pending->handler_returned))) {
            if (r.changed.wait_until(lock, pending->deadline) == std::cv_status::timeout &&
                (pending->result == AuthInteractionResult::NotHandled || !pending->handler_returned))
                retire(r, AuthInteractionResult::TimedOut);
        }
        result = pending->result;
        if (r.terminal != AuthInteractionResult::NotHandled) result = r.terminal;
        if (result == AuthInteractionResult::Answered) answer = std::move(pending->response);
        if (r.pending == pending) r.pending.reset();
    }
    return result;
}

AuthInteractionSubmitResult submit_auth_interaction_response(AuthInteractionResponse response) {
    using Submit = AuthInteractionSubmitResult;
    auto& r = runtime(); std::lock_guard<std::mutex> lock(r.mutex);
    const auto pending = r.pending;
    if (!pending) return Submit::NoPendingRequest;
    const auto& q = pending->schema;
    if (response.attempt_id != q.attempt_id || response.request_id != q.request_id) return Submit::WrongRequest;
    if (Clock::now() >= pending->deadline) {
        if (pending->result == AuthInteractionResult::NotHandled) retire(r, AuthInteractionResult::TimedOut);
        return Submit::Expired;
    }
    if (pending->result != AuthInteractionResult::NotHandled) return Submit::AlreadyAnswered;
    if (response.protocol_version != AuthInteractionLimits::Version) return Submit::InvalidResponse;
    if (response.disposition != AuthInteractionDisposition::Submit) {
        if (response.button_id || !response.values.empty() ||
            (response.disposition != AuthInteractionDisposition::Cancel &&
             response.disposition != AuthInteractionDisposition::Unsupported)) return Submit::InvalidResponse;
        if (q.kind == AuthInteractionKind::Unsupported || response.disposition == AuthInteractionDisposition::Unsupported)
            retire(r, AuthInteractionResult::Unsupported);
        else retire(r, AuthInteractionResult::Cancelled);
        return Submit::Accepted;
    }
    if (q.kind == AuthInteractionKind::Unsupported || response.values.size() > AuthInteractionLimits::Fields)
        return Submit::InvalidResponse;
    const AuthInteractionButton* button = nullptr;
    for (const auto& b : q.buttons) if (b.id == response.button_id && b.enabled) button = &b;
    if (!button) return Submit::InvalidResponse;
    if (button->role == AuthInteractionButtonRole::Cancel && !response.values.empty()) return Submit::InvalidResponse;
    size_t total = 0;
    for (size_t i = 0; i < response.values.size(); ++i) {
        const auto& v = response.values[i];
        const AuthInteractionField* field = nullptr;
        for (const auto& f : q.fields) if (f.id == v.field_id) field = &f;
        if (!field || v.value.size() < field->min_bytes || v.value.size() > field->max_bytes ||
            !auth_interaction_utf8(v.value) || field->type == AuthInteractionFieldType::Unsupported ||
            (field->digits_only && v.value.find_first_not_of("0123456789") != std::string::npos)) return Submit::InvalidResponse;
        for (size_t j = 0; j < i; ++j) if (response.values[j].field_id == v.field_id) return Submit::InvalidResponse;
        total += v.value.size();
    }
    if (total > AuthInteractionLimits::InputTotal) return Submit::InvalidResponse;
    if (button->role != AuthInteractionButtonRole::Cancel) for (const auto& f : q.fields) {
        bool found = false;
        for (const auto& v : response.values) if (v.field_id == f.id) found = true;
        if (f.required && !found) return Submit::InvalidResponse;
    }
    pending->response = std::move(response);
    pending->result = AuthInteractionResult::Answered;
    r.changed.notify_all();
    return Submit::Accepted;
}
