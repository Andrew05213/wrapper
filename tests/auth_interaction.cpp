#include "../lite/auth_interaction_internal.h"
#include <cassert>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

using Result = AuthInteractionResult;
using Submit = AuthInteractionSubmitResult;
using Disposition = AuthInteractionDisposition;

static AuthInteractionRequest request() {
    AuthInteractionRequest q;
    q.kind = AuthInteractionKind::ProtocolDialog;
    q.title = "登录 SECRET-title"; q.message = "本次请求 SECRET-message";
    AuthInteractionButton b; b.id = auth_interaction_item_id(); b.label = "继续";
    q.buttons.push_back(b);
    AuthInteractionField f; f.id = auth_interaction_item_id(); f.type = AuthInteractionFieldType::Secret;
    f.label = "私密输入"; f.max_bytes = 64;
    q.fields.push_back(f);
    return q;
}
static AuthInteractionResponse answer(const AuthInteractionRequest& q) {
    AuthInteractionResponse a;
    a.attempt_id = q.attempt_id; a.request_id = q.request_id;
    a.disposition = Disposition::Submit; a.button_id = q.buttons[0].id;
    AuthInteractionValue v; v.field_id = q.fields[0].id; v.value = "SECRET-answer";
    a.values.push_back(v); return a;
}
static void workers_done() {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (auth_interaction_worker_count() && std::chrono::steady_clock::now() < until)
        std::this_thread::yield();
    assert(auth_interaction_worker_count() == 0);
}
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool open = false;
    void wait() { std::unique_lock<std::mutex> lock(mutex); changed.wait(lock, [&]{ return open; }); }
    void release() { std::lock_guard<std::mutex> lock(mutex); open = true; changed.notify_all(); }
};
struct Session {
    std::atomic<int>* destroyed;
    explicit Session(std::atomic<int>* value) : destroyed(value) {}
    ~Session() { assert(set_auth_interaction_handler({})); ++*destroyed; }
};

int main() {
    assert(!set_auth_interaction_handler({}, 0));
    assert(!set_auth_interaction_handler({}, AuthInteractionLimits::MaxTimeoutMs + 1));
    uint64_t id = auth_interaction_begin_attempt(42);
    assert(current_auth_interaction_attempt() == id);
    assert(id && !auth_interaction_handles_attempt());
    AuthInteractionResponse a;
    assert(auth_interaction_ask(request(), a) == Result::NotHandled);
    assert(auth_interaction_end_attempt(id) == Result::NotHandled);
    assert(!current_auth_interaction_attempt());
    id = auth_interaction_begin_attempt(43); // Handler adoption on Started, then loss before first request.
    assert(set_auth_interaction_handler([](AuthInteractionRequest) {}, 1000));
    assert(set_auth_interaction_handler({}));
    assert(auth_interaction_ask(request(), a) == Result::Unavailable);
    assert(auth_interaction_end_attempt(id) == Result::Unavailable);

    uint64_t previous_request = 0;
    for (int i = 0; i < 32; ++i) {
        auto done = std::make_shared<std::promise<void>>();
        auto future = done->get_future();
        assert(set_auth_interaction_handler([&, done](AuthInteractionRequest q) {
            assert(q.protocol_version == 1 && q.downstream_attempt_id == 42 && q.request_id > previous_request);
            assert(auth_interaction_handles_attempt()); // Reentrant query, no mutex held.
            assert(q.title == "登录 SECRET-title");
            auto response = answer(q);
            auto forged = response; ++forged.request_id;
            assert(submit_auth_interaction_response(std::move(forged)) == Submit::WrongRequest);
            forged = response; ++forged.attempt_id;
            assert(submit_auth_interaction_response(std::move(forged)) == Submit::WrongRequest);
            forged = response; forged.protocol_version = 2;
            assert(submit_auth_interaction_response(std::move(forged)) == Submit::InvalidResponse);
            forged = response; ++forged.button_id;
            assert(submit_auth_interaction_response(std::move(forged)) == Submit::InvalidResponse);
            forged = response; ++forged.values[0].field_id;
            assert(submit_auth_interaction_response(std::move(forged)) == Submit::InvalidResponse);
            forged = response; forged.values.push_back(forged.values[0]);
            assert(submit_auth_interaction_response(std::move(forged)) == Submit::InvalidResponse);
            forged = response; forged.values[0].value = std::string("a\0b", 3);
            assert(submit_auth_interaction_response(std::move(forged)) == Submit::InvalidResponse);
            forged = response; forged.values[0].value.assign(65, 'x');
            assert(submit_auth_interaction_response(std::move(forged)) == Submit::InvalidResponse);
            previous_request = q.request_id;
            assert(submit_auth_interaction_response(response) == Submit::Accepted);
            const auto duplicate = submit_auth_interaction_response(std::move(response));
            assert(duplicate == Submit::AlreadyAnswered || duplicate == Submit::NoPendingRequest);
            done->set_value();
        }, 1000));
        id = auth_interaction_begin_attempt(42);
        assert(id);
        assert(auth_interaction_ask(request(), a) == Result::Answered);
        assert(a.values[0].value == "SECRET-answer");
        assert(future.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        assert(auth_interaction_end_attempt(id) == Result::NotHandled);
        workers_done();
        assert(submit_auth_interaction_response(a) == Submit::NoPendingRequest);
    }

    // Several dialogs in one attempt have distinct IDs and cannot consume an old answer.
    AuthInteractionResponse old;
    assert(set_auth_interaction_handler([&](AuthInteractionRequest q) {
        if (old.request_id) assert(submit_auth_interaction_response(old) == Submit::WrongRequest);
        auto response = answer(q); old = response;
        assert(submit_auth_interaction_response(std::move(response)) == Submit::Accepted);
    }, 1000));
    id = auth_interaction_begin_attempt(7);
    for (int i = 0; i < 8; ++i) {
        assert(auth_interaction_ask(request(), a) == Result::Answered);
        workers_done();
    }
    auth_interaction_end_attempt(id);

    for (int mode = 0; mode < 6; ++mode) {
        assert(set_auth_interaction_handler([mode](AuthInteractionRequest q) {
            if (mode == 0) {
                AuthInteractionResponse response;
                response.attempt_id = q.attempt_id; response.request_id = q.request_id;
                assert(submit_auth_interaction_response(std::move(response)) == Submit::Accepted);
            } else if (mode == 1) assert(set_auth_interaction_handler({})); // self-unregister
            else if (mode == 2) throw std::runtime_error("SECRET-handler-exception");
            else if (mode == 3) assert(cancel_auth_interaction(q.attempt_id));
            else {
                assert(submit_auth_interaction_response(answer(q)) == Submit::Accepted);
                if (mode == 4) assert(set_auth_interaction_handler({}));
                else throw std::runtime_error("SECRET-after-submit-exception");
            }
        }, 1000));
        id = auth_interaction_begin_attempt(1);
        const auto result = auth_interaction_ask(request(), a);
        assert(result == (mode == 1 || mode == 4 ? Result::Unavailable : mode == 2 || mode == 5 ? Result::HandlerFailed : Result::Cancelled));
        assert(auth_interaction_ask(request(), a) == result); // Retired attempt cannot resume.
        auth_interaction_end_attempt(id); workers_done();
    }

    auto gate = std::make_shared<Gate>();
    auto began = std::make_shared<std::promise<AuthInteractionRequest>>();
    auto seen = began->get_future();
    assert(set_auth_interaction_handler([gate, began](AuthInteractionRequest q) {
        began->set_value(q); gate->wait(); // Even a blocked handler cannot block SDK timeout.
    }, 100));
    id = auth_interaction_begin_attempt(5);
    const auto start = std::chrono::steady_clock::now();
    assert(auth_interaction_ask(request(), a) == Result::TimedOut);
    assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    assert(seen.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    assert(submit_auth_interaction_response(answer(seen.get())) != Submit::Accepted);
    assert(auth_interaction_end_attempt(id) == Result::TimedOut);
    assert(set_auth_interaction_handler({}));
    gate->release(); workers_done();

    // An accepted answer followed by a stuck/failed dispatch must not become success.
    gate = std::make_shared<Gate>();
    assert(set_auth_interaction_handler([gate](AuthInteractionRequest q) {
        assert(submit_auth_interaction_response(answer(q)) == Submit::Accepted);
        gate->wait();
    }, 100));
    id = auth_interaction_begin_attempt(0);
    assert(auth_interaction_ask(request(), a) == Result::TimedOut);
    auth_interaction_end_attempt(id); gate->release(); workers_done();

    std::atomic<int> destroyed{0};
    auto session = std::make_shared<Session>(&destroyed);
    assert(set_auth_interaction_handler([session](AuthInteractionRequest q) {
        assert(submit_auth_interaction_response(answer(q)) == Submit::Accepted);
    }, 1000));
    session.reset();
    id = auth_interaction_begin_attempt(0);
    assert(auth_interaction_ask(request(), a) == Result::Answered);
    auth_interaction_end_attempt(id); workers_done();
    assert(set_auth_interaction_handler({})); // Capture destruction reenters registration.
    assert(destroyed == 1);

    // Enforce the fixed worker budget across timed-out attempts.
    gate = std::make_shared<Gate>();
    for (size_t i = 0; i < AuthInteractionLimits::Workers; ++i) {
        auto started = std::make_shared<std::promise<void>>(); auto ready = started->get_future();
        assert(set_auth_interaction_handler([gate, started](AuthInteractionRequest) { started->set_value(); gate->wait(); }, 100));
        id = auth_interaction_begin_attempt(0);
        assert(auth_interaction_ask(request(), a) == Result::TimedOut);
        assert(ready.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
        auth_interaction_end_attempt(id);
    }
    id = auth_interaction_begin_attempt(0);
    assert(auth_interaction_ask(request(), a) == Result::Unavailable);
    auth_interaction_end_attempt(id); set_auth_interaction_handler({});
    gate->release(); workers_done();

    assert(set_auth_interaction_handler([](AuthInteractionRequest q) {
        auto response = answer(q);
        assert(submit_auth_interaction_response(std::move(response)) == Submit::InvalidResponse);
        AuthInteractionResponse ack; ack.attempt_id = q.attempt_id; ack.request_id = q.request_id;
        ack.disposition = Disposition::Unsupported;
        assert(submit_auth_interaction_response(std::move(ack)) == Submit::Accepted);
    }, 1000));
    id = auth_interaction_begin_attempt(0);
    auto unknown = request(); unknown.kind = AuthInteractionKind::Unsupported;
    unknown.unsupported = AuthInteractionUnsupported::FieldType;
    assert(auth_interaction_ask(std::move(unknown), a) == Result::Unsupported);
    auth_interaction_end_attempt(id); set_auth_interaction_handler({}); workers_done();
    assert(!cancel_auth_interaction(id));
    assert(!auth_interaction_utf8(std::string("\xed\xa0\x80", 3)));
    assert(!auth_interaction_utf8(std::string("\xc0\xaf", 2)));
    puts("UI interaction: correlation, validation, cancellation, deadlines, reentry and bounded workers passed");
}
