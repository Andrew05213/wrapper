// Exercise the production native bridge with a refcounted controller cache.
#include "../lite/playback.cpp"
#include <cassert>
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
struct Resource {
    int refs = 1;
    bool key;
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
    memcpy(output, input, size);
    return 0;
}
int main() {
    const std::string a(32, 'a'), b(32, 'b'), sample(16, 'x');
    const std::string uri = "skd://itunes.apple.com/content";
    std::string out;
    for (int i = 0; i < 2048; ++i) {
        assert(native_decrypt(a, "1", uri, sample, out));
        assert(keys == 0 && contexts == 1 && cache.empty() && out == sample);
        assert(native_decrypt(b, "2", uri, sample, out));
        assert(contexts == 2 && cache.empty());
        native_decrypt_close(a);
        assert(contexts == 1);
        assert(native_decrypt(b, "2", uri, sample, out));
        native_decrypt_close(b);
        assert(contexts == 0 && keys == 0);
    }
    fail_context = true;
    try { native_decrypt(a, "1", uri, sample, out); assert(false); }
    catch (const std::runtime_error&) {}
    assert(live.empty() && cache.empty());
    fail_context = false;
    throw_sample = true;
    try { native_decrypt(a, "1", uri, sample, out); assert(false); }
    catch (const std::runtime_error&) {}
    assert(live.empty() && cache.empty());
    throw_sample = false;
    assert(!native_decrypt(a, "1", "skd://itunes.apple.com/P000000000/s1/e1", sample, out));
    assert(!native_decrypt(a, "1", uri, "bad size", out));
    assert(!native_decrypt("bad stream", "1", uri, sample, out));
    assert(!native_decrypt(a, "1", uri + std::string(1, '\0') + "suffix", sample, out));
    assert(native_decrypt(a, "1", uri, sample, out));
    native_decrypt_close_all();
    assert(live.empty() && cache.empty());
    puts("native bridge: 4096 stream lifetimes, cache detachment and exceptions passed");
}
