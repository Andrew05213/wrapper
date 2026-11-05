// Host regression test: exercise real M3U8 code with owning library returns.
// Compile with -ffunction-sections -Wl,--gc-sections and Dobby's include path.
#include "../lite/playback.cpp"
#include <cassert>
#include <stdexcept>

struct shared_ptr g_reqCtx = {};
uint8_t leaseMgr[256] = {};
std::mutex g_playback_mutex;
int offlineFlag = 0;

extern "C" long NfcRKVnxuKZy04KWbdFu71Ou(void*, uint32_t, void*, void*, size_t) {
    assert(false); // M3U8 tests must never perform sample decryption.
    return 0;
}

static int live = 0, requests = 0, strings = 0;
static int mode = 0;
struct resource { int refs = 1; resource() { ++live; } ~resource() { --live; } };
static struct shared_ptr owned() {
    auto* p = new resource;
    return {p, p};
}
static struct std_vector vector_of_one() {
    auto* p = static_cast<struct shared_ptr*>(::operator new(sizeof(struct shared_ptr)));
    *p = owned();
    return {p, p + 1, p + 1};
}
static void output_string(union std_string* out) {
    const char* text = "https://example.test/a/long/playlist.m3u8";
    *out = new_std_string(strdup(text));
    ++strings;
}
extern "C" void _ZNSt6__ndk119__shared_weak_count16__release_sharedEv(void* p) {
    auto* r = static_cast<resource*>(p);
    if (--r->refs == 0) delete r;
}
extern "C" void _ZNSt6__ndk112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEED2Ev(union std_string* p) {
    if (p->mark & 1) { free(const_cast<char*>(p->data)); --strings; }
}
extern "C" void* _ZN17storeservicescore15PurchaseRequestC2ERKNSt6__ndk110shared_ptrINS_14RequestContextEEE(void* p, struct shared_ptr*) {
    ++requests;
    *static_cast<struct shared_ptr*>(p) = owned();
    return p;
}
extern "C" void _ZN17storeservicescore15PurchaseRequestD2Ev(void* p) {
    --requests;
    _ZNSt6__ndk119__shared_weak_count16__release_sharedEv(static_cast<struct shared_ptr*>(p)->ctrl_blk);
}
extern "C" void* _ZN17storeservicescore15PurchaseRequest23setProcessDialogActionsEb(void* p, int) { return p; }
extern "C" void* _ZN17storeservicescore15PurchaseRequest12setURLBagKeyERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void* p, union std_string*) { return p; }
extern "C" void* _ZN17storeservicescore15PurchaseRequest16setBuyParametersERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void* p, union std_string*) { return p; }
extern "C" void* _ZN17storeservicescore15PurchaseRequest3runEv(void* p) {
    if (mode == 1) throw std::runtime_error("purchase failed");
    return p;
}
extern "C" struct shared_ptr* _ZNK17storeservicescore15PurchaseRequest8responseEv(void* p) {
    return mode == 2 ? nullptr : static_cast<struct shared_ptr*>(p);
}
extern "C" struct shared_ptr* _ZN17storeservicescore16PurchaseResponse5errorEv(void*) {
    static struct shared_ptr error = {};
    error.obj = mode == 3 ? &error : nullptr;
    return &error;
}
extern "C" struct std_vector _ZNK17storeservicescore16PurchaseResponse5itemsEv(void*) {
    return mode == 4 ? std_vector{} : vector_of_one();
}
extern "C" struct std_vector _ZNK17storeservicescore12PurchaseItem6assetsEv(void*) {
    if (mode == 5) throw std::runtime_error("assets failed");
    return mode == 6 ? std_vector{} : vector_of_one();
}
extern "C" union std_string* _ZNK17storeservicescore13PurchaseAsset3URLEv(void* out, void*) {
    if (mode == 7) throw std::runtime_error("URL failed");
    output_string(static_cast<union std_string*>(out));
    return static_cast<union std_string*>(out);
}
extern "C" struct shared_ptr* _ZN22SVPlaybackLeaseManager12requestAssetERKmRKNSt6__ndk16vectorINS2_12basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEENS7_IS9_EEEERKb(
    void* output, void*, unsigned long*, struct std_vector* formats, uint8_t*) {
    auto* out = static_cast<struct shared_ptr*>(output);
    assert(static_cast<char*>(formats->end) - static_cast<char*>(formats->begin) == sizeof(union std_string));
    assert(strcmp(std_string_data(static_cast<union std_string*>(formats->begin)), "HLS") == 0);
    if (mode != 2) *out = owned();
    return out;
}
extern "C" int _ZNK23SVPlaybackAssetResponse13hasValidAssetEv(void*) { return mode != 3; }
extern "C" struct shared_ptr* _ZNK23SVPlaybackAssetResponse13playbackAssetEv(void*) {
    static struct shared_ptr borrowed = {&borrowed, nullptr};
    return mode == 4 ? nullptr : &borrowed;
}
extern "C" union std_string* _ZNK17storeservicescore13PlaybackAsset9URLStringEv(void* out, uint8_t*) {
    if (mode == 5) throw std::runtime_error("play URL failed");
    output_string(static_cast<union std_string*>(out));
    return static_cast<union std_string*>(out);
}
int main() {
    for (int iteration = 0; iteration < 1000; ++iteration) {
        for (mode = 0; mode < 8; ++mode) {
            try {
                const char* url = get_m3u8_download(123);
                assert((url != nullptr) == (mode == 0));
                if (url) assert(strstr(url, "playlist.m3u8"));
                free(const_cast<char*>(url));
                assert(mode != 1 && mode != 5 && mode != 7);
            } catch (const std::runtime_error&) {
                assert(mode == 1 || mode == 5 || mode == 7);
            }
            assert(live == 0 && requests == 0 && strings == 0);
            try {
                const char* url = get_m3u8_play(123);
                assert((url != nullptr) == (mode == 0 || mode == 1 || mode == 6 || mode == 7));
                if (url) assert(strstr(url, "playlist.m3u8"));
                free(const_cast<char*>(url));
                assert(mode != 5);
            } catch (const std::runtime_error&) {
                assert(mode == 5);
            }
            assert(live == 0 && requests == 0 && strings == 0);
        }
    }
    puts("16000 M3U8 success/error/exception cases: all owners released");
}
