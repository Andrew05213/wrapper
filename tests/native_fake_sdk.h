#pragma once
// Host-only stand-ins for SDK ownership and stateful, in-place block processing.
#include "../lite/playback.cpp"
#include <cassert>
#include <atomic>
#include <stdexcept>
#include <set>
#include <vector>

struct shared_ptr g_reqCtx = {};
uint8_t leaseMgr[256] = {};
std::mutex g_playback_mutex;
void* FHinstance = reinterpret_cast<void*>(1);
int offlineFlag = 0;
static int keys = 0, contexts = 0;
static bool fail_context = false, throw_sample = false;
static unsigned fail_on_block = 0, cancel_on_block = 0;
static std::atomic<bool> cancelled{false};
struct Resource {
    int refs = 1;
    bool key;
    unsigned sequence = 0;
    union std_string value = new_std_string("persistent-key");
    void* engine = this;
    void* wrapper = &engine;
    explicit Resource(bool k) : key(k) { if (key) ++keys; else ++contexts; }
    ~Resource() { if (key) --keys; else --contexts; }
};
static std::vector<Resource*> cache;
static std::set<Resource*> live;
extern "C" void _ZNSt6__ndk119__shared_weak_count16__release_sharedEv(void* p) {
    auto* r = static_cast<Resource*>(p);
    assert(live.count(r));
    if (--r->refs == 0) { live.erase(r); delete r; }
}
extern "C" void _ZN21SVFootHillSessionCtrl16resetAllContextsEv(void*) {
    for (auto* r : cache) _ZNSt6__ndk119__shared_weak_count16__release_sharedEv(r);
    cache.clear();
}
extern "C" void _ZN21SVFootHillSessionCtrl16getPersistentKeyERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEES8_S8_S8_S8_S8_S8_S8_(
    struct shared_ptr* out, void*, union std_string*, union std_string*, union std_string*, union std_string*,
    union std_string*, union std_string*, union std_string*, union std_string*) {
    auto* r = new Resource(true);
    live.insert(r);
    *out = {&r->value, r};
}
extern "C" void _ZN21SVFootHillSessionCtrl14decryptContextERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEERKN11SVDecryptor15SVDecryptorTypeERKb(
    struct shared_ptr* out, void*, union std_string*) {
    auto* r = new Resource(false);
    live.insert(r);
    *out = {r, r};
    ++r->refs; // the library's cache owns a second reference
    cache.push_back(r);
    if (fail_context) throw std::runtime_error("context creation failed");
}
extern "C" void** _ZNK18SVFootHillPContext9kdContextEv(void* p) {
    return &static_cast<Resource*>(p)->wrapper;
}
extern "C" long NfcRKVnxuKZy04KWbdFu71Ou(void* p, uint32_t, void* input, void* output, size_t size) {
    auto* r = static_cast<Resource*>(p);
    assert(live.count(r) && !r->key); // a cache reset must not kill active owners
    if (throw_sample) throw std::runtime_error("decrypt failed");
    assert(input == output);
    // Cheap synthetic work. The evolving sequence detects missing boundaries,
    // reordered calls, shared state and accidental context recreation.
    auto* bytes = static_cast<unsigned char*>(output);
    auto mask = static_cast<unsigned char>(++r->sequence);
    if (fail_on_block == r->sequence) throw std::runtime_error("synthetic failure");
    if (cancel_on_block == r->sequence) cancelled.store(true);
    for (size_t i = 0; i < size; i += 16)
        bytes[i] ^= mask;
    return 0;
}
