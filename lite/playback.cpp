#include "internal.h"
#include "library_ownership.h"
#include "native_streams.h"
#include <cstddef>
#include <cinttypes>
#include <atomic>
#include <condition_variable>
#include <deque>
#include "dobby.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <mutex>
#include <memory>
#include <dlfcn.h>
#include <link.h>

extern const char* const fairplayCert;

namespace {
// FIFO when the private batch transport is opted in. A batch joins at the tail
// again for every block, so earlier single requests and downloads both advance.
class PlaybackAdmission {
public:
    void lock() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!active_) {
            active_ = true;
            return;
        }
        Waiter waiter;
        waiters_.push_back(&waiter);
        waiter.ready.wait(lock, [&] { return waiter.granted; });
    }

    void unlock() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (waiters_.empty()) {
            active_ = false;
            return;
        }
        auto* next = waiters_.front();
        waiters_.pop_front();
        next->granted = true;
        next->ready.notify_one();
    }

    size_t waiting() {
        std::lock_guard<std::mutex> lock(mutex_);
        return waiters_.size();
    }

private:
    struct Waiter {
        std::condition_variable ready;
        bool granted = false;
    };
    std::mutex mutex_;
    std::deque<Waiter*> waiters_;
    bool active_ = false;
};

PlaybackAdmission playback_admission;
std::atomic<bool> native_batch_enabled{false};

std::unique_lock<PlaybackAdmission> take_playback_turn() {
    std::unique_lock<PlaybackAdmission> turn(playback_admission, std::defer_lock);
    if (native_batch_enabled.load(std::memory_order_relaxed)) turn.lock();
    return turn;
}

struct PlaybackLock {
    std::unique_lock<PlaybackAdmission> turn = take_playback_turn();
    std::lock_guard<std::mutex> sdk{g_playback_mutex};
};
}

const char* get_m3u8_download(unsigned long adam) {
    alignas(std::max_align_t) uint8_t purchase_request[1024] = {};
    _ZN17storeservicescore15PurchaseRequestC2ERKNSt6__ndk110shared_ptrINS_14RequestContextEEE(purchase_request, &g_reqCtx);
    library_object_guard request_owner(purchase_request, _ZN17storeservicescore15PurchaseRequestD2Ev);
    _ZN17storeservicescore15PurchaseRequest23setProcessDialogActionsEb(purchase_request, 1);
    union std_string urlBagKey = new_std_string("subDownload");
    _ZN17storeservicescore15PurchaseRequest12setURLBagKeyERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(purchase_request, &urlBagKey);
    char buyParams[128];
    snprintf(buyParams, sizeof(buyParams), "salableAdamId=%lu&price=0&pricingParameters=SUBS&productType=S", adam);
    union std_string bp = new_std_string(buyParams);
    _ZN17storeservicescore15PurchaseRequest16setBuyParametersERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(purchase_request, &bp);
    _ZN17storeservicescore15PurchaseRequest3runEv(purchase_request);
    struct shared_ptr* response = _ZNK17storeservicescore15PurchaseRequest8responseEv(purchase_request);
    if (!response || !response->obj) return nullptr;
    struct shared_ptr* error = _ZN17storeservicescore16PurchaseResponse5errorEv(response->obj);
    if (!error || error->obj) return nullptr;

    library_shared_vector items(_ZNK17storeservicescore16PurchaseResponse5itemsEv(response->obj));
    if (!items.value.begin || items.value.begin == items.value.end) return nullptr;
    auto* firstItem = static_cast<struct shared_ptr*>(items.value.begin);
    if (!firstItem->obj) return nullptr;
    library_shared_vector assets(_ZNK17storeservicescore12PurchaseItem6assetsEv(firstItem->obj));
    if (!assets.value.begin || assets.value.begin == assets.value.end) return nullptr;
    auto* lastAsset = static_cast<struct shared_ptr*>(assets.value.end) - 1;
    if (!lastAsset->obj) return nullptr;
    library_string url;
    _ZNK17storeservicescore13PurchaseAsset3URLEv(&url.value, lastAsset->obj);
    return url.data() ? strdup(url.data()) : nullptr;
}

const char* get_m3u8_play(unsigned long adam, int* authorization_status) {
    union std_string HLS = new_std_string("HLS");
    /* vector<string> bounds are byte addresses spanning one entire string. */
    struct std_vector HLSParam = {&HLS, &HLS + 1, &HLS + 1};

    uint8_t z0 = 0;
    library_shared_ptr result;
    _ZN22SVPlaybackLeaseManager12requestAssetERKmRKNSt6__ndk16vectorINS2_12basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEENS7_IS9_EEEERKb(
        &result.value, leaseMgr, &adam, &HLSParam, &z0);

    if (!result.value.obj) return nullptr;
    if (!_ZNK23SVPlaybackAssetResponse13hasValidAssetEv(result.value.obj)) {
        if (authorization_status) {
            const int* code = _ZNK23SVPlaybackAssetResponse9errorCodeEv(result.value.obj);
            if (code && (*code == 401 || *code == 403)) *authorization_status = *code;
        }
        return nullptr;
    }

    struct shared_ptr* playbackAsset = _ZNK23SVPlaybackAssetResponse13playbackAssetEv(result.value.obj);
    if (!playbackAsset || !playbackAsset->obj) return nullptr;

    library_string m3u8;
    _ZNK17storeservicescore13PlaybackAsset9URLStringEv(&m3u8.value, static_cast<uint8_t*>(playbackAsset->obj));
    return m3u8.data() ? strdup(m3u8.data()) : nullptr;
}

/* getPersistentKey() returns an owning shared_ptr, and nothing in the library
   drops that reference for us: every key fetched and kept leaks a few KB. */
static void release_persistent_key(struct shared_ptr* key) {
    if (key->ctrl_blk) _ZNSt6__ndk119__shared_weak_count16__release_sharedEv(key->ctrl_blk);
    key->obj = nullptr; key->ctrl_blk = nullptr;
}

/* Releases the key when the scope ends, including when the library throws
   (an Invalid CKC from Apple reaches the HTTP handler as an exception). */
struct persistent_key {
    struct shared_ptr p = {.obj = nullptr, .ctrl_blk = nullptr};
    persistent_key() = default;
    persistent_key(const persistent_key&) = delete;
    persistent_key& operator=(const persistent_key&) = delete;
    ~persistent_key() { release_persistent_key(&p); }
};

/* Fetches the persistent key, one license round trip to Apple, and hands it to
   the caller in persistK so the template capture can reuse it. */
static char* get_content_key_impl(const std::string& adamId, const std::string& keyUri,
                                  struct shared_ptr* persistK) {
    union std_string defaultId = new_std_string(adamId.c_str());
    union std_string keyUriStr = new_std_string(keyUri.c_str());
    union std_string keyFormat = new_std_string("com.apple.streamingkeydelivery");
    union std_string keyFormatVer = new_std_string("1");
    union std_string serverUri = new_std_string("https://play.itunes.apple.com/WebObjects/MZPlay.woa/music/fps");
    union std_string protocolType = new_std_string("simplified");
    union std_string fpsCertStr = new_std_string(fairplayCert);

    release_persistent_key(persistK);
    _ZN21SVFootHillSessionCtrl16getPersistentKeyERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEES8_S8_S8_S8_S8_S8_S8_(
        persistK, FHinstance, &defaultId, &defaultId, &keyUriStr, &keyFormat,
        &keyFormatVer, &serverUri, &protocolType, &fpsCertStr);

    if (!persistK->obj) return nullptr;
    union std_string* pkey = (union std_string*)persistK->obj;
    const char* data = std_string_data(pkey);
    if (!data || !*data) return nullptr;
    return strdup(data);
}

static volatile int g_cap_armed = 0;
static volatile int g_cap_done = 0;
static int g_r1_hooked = 0;
static uint8_t g_cap_ctx[0x8000];
static uint8_t g_cap_state[0x2100];
static uint64_t g_cap_rcx, g_cap_rax, g_cap_rdx, g_cap_r9, g_cap_rbp;

extern "C" void r1_capture_cb(RegisterContext* ctx, const HookEntryInfo* info) {
    (void)info;
    if (!g_cap_armed || g_cap_done) return;
    if ((ctx->general.regs.rsi & 0xff) != 8) return;
    uint64_t r9 = ctx->general.regs.r9;
    uint64_t rbp = ctx->general.regs.rbp;
    memcpy(g_cap_ctx, (void*)(uintptr_t)r9, 0x8000);
    memcpy(g_cap_state, (void*)(uintptr_t)(rbp - 0x2000), 0x2100);
    g_cap_rcx = ctx->general.regs.rcx;
    g_cap_rax = ctx->general.regs.rax;
    g_cap_rdx = ctx->general.regs.rdx;
    g_cap_r9 = r9;
    g_cap_rbp = rbp;
    g_cap_done = 1;
}

static int _find_lib_cb(struct dl_phdr_info* info, size_t size, void* data) {
    (void)size;
    if (info->dlpi_name && strstr(info->dlpi_name, "libCoreLSKD.so")) {
        *(uintptr_t*)data = info->dlpi_addr;
        return 1;
    }
    return 0;
}
static uintptr_t get_lib_core_lskd_base(void) {
    uintptr_t base = 0;
    dl_iterate_phdr(_find_lib_cb, &base);
    return base;
}
static void setup_r1_hook(void) {
    if (g_r1_hooked) return;
    uintptr_t base = get_lib_core_lskd_base();
    if (!base) { LOG_WARN("libCoreLSKD not loaded"); return; }
    void* r1 = (void*)(base + 0x1d5709);
    if (DobbyInstrument(r1, r1_capture_cb) == 0) {
        g_r1_hooked = 1;
        LOG_DEBUG("R1 hook installed @ %p (base 0x%lx)", r1, base);
    } else {
        LOG_WARN("R1 hook install failed @ %p", r1);
    }
}

static void* preshareCtx = nullptr;
static struct shared_ptr preshareOwner = {.obj = nullptr, .ctrl_blk = nullptr};
static struct shared_ptr preshareKey = {.obj = nullptr, .ctrl_blk = nullptr};
/* known, when set, is the persistent key get_content_key_impl already fetched
   for this adam and uri; without it the key is fetched again into fetched,
   which the caller releases after the context made from it. */
static void* getKdContext(const std::string& adam, const std::string& uri, struct shared_ptr* owner,
                          const struct shared_ptr* known, struct shared_ptr* fetched) {
    uint8_t isPreshare = (adam == "0");
    if (isPreshare && preshareCtx) return preshareCtx;
    LOG_DEBUG("playback_context_requested");

    union std_string defaultId = new_std_string(adam.c_str());
    union std_string keyUri = new_std_string(uri.c_str());
    union std_string keyFormat = new_std_string("com.apple.streamingkeydelivery");
    union std_string keyFormatVer = new_std_string("1");
    union std_string serverUri = new_std_string("https://play.itunes.apple.com/WebObjects/MZPlay.woa/music/fps");
    union std_string protocolType = new_std_string("simplified");
    union std_string fpsCert = new_std_string(fairplayCert);

    const struct shared_ptr* persistK = known;
    if (!persistK || !persistK->obj) {
        release_persistent_key(fetched);
        _ZN21SVFootHillSessionCtrl16getPersistentKeyERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEES8_S8_S8_S8_S8_S8_S8_(
            fetched, FHinstance, &defaultId, &defaultId, &keyUri, &keyFormat,
            &keyFormatVer, &serverUri, &protocolType, &fpsCert);
        persistK = fetched;
    }
    if (!persistK->obj) return nullptr;

    struct shared_ptr SVFootHillPContext = {.obj = nullptr, .ctrl_blk = nullptr};
    _ZN21SVFootHillSessionCtrl14decryptContextERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEERKN11SVDecryptor15SVDecryptorTypeERKb(
        &SVFootHillPContext, FHinstance, (union std_string*)persistK->obj);
    if (!SVFootHillPContext.obj) return nullptr;

    void* kdContext = *_ZNK18SVFootHillPContext9kdContextEv(SVFootHillPContext.obj);
    if (kdContext && isPreshare) {
        preshareCtx = kdContext;
        preshareOwner = SVFootHillPContext;
    } else {
        *owner = SVFootHillPContext;
    }
    return kdContext;
}

/* Every decryptContext() result is held twice: by the shared_ptr it returns
   and by the session controller's persistent-key context map, which only
   resetAllContexts() empties. Keeping either alive leaks the FairPlay context,
   and after 1024 of them every /key fails with KDCanProcessCKC -42786. A
   captured template is a copy, so drop both once it has been taken. */
static void release_decrypt_ctxs(struct shared_ptr* owner) {
    if (owner->obj) _ZNSt6__ndk110shared_ptrI18SVFootHillPContextED2Ev(owner);
    owner->obj = nullptr; owner->ctrl_blk = nullptr;
    if (preshareOwner.obj) _ZNSt6__ndk110shared_ptrI18SVFootHillPContextED2Ev(&preshareOwner);
    preshareOwner.obj = nullptr; preshareOwner.ctrl_blk = nullptr;
    preshareCtx = nullptr;
    _ZN21SVFootHillSessionCtrl16resetAllContextsEv(FHinstance);
    release_persistent_key(&preshareKey);
}

static void refresh_decrypt_ctx() {
    uint8_t autom = 1;
    _ZN22SVPlaybackLeaseManager12requestLeaseERKb(leaseMgr, &autom);
    struct shared_ptr none = {.obj = nullptr, .ctrl_blk = nullptr};
    release_decrypt_ctxs(&none);
    preshareCtx = getKdContext("0", "skd://itunes.apple.com/P000000000/s1/e1", &none, nullptr, &preshareKey);
    LOG_WARN("refreshed context");
}

static int capture_content_template(const std::string& adam, const std::string& uri,
                                     uint8_t* cap_ctx, uint8_t* cap_state,
                                     uint64_t* rcx, uint64_t* rax, uint64_t* rdx,
                                     uint64_t* r9, uint64_t* rbp,
                                     const struct shared_ptr* known, int with_refresh) {
    if (with_refresh) refresh_decrypt_ctx();
    persistent_key fetched;
    struct shared_ptr owner = {.obj = nullptr, .ctrl_blk = nullptr};
    library_object_guard context_cleanup(&owner, [](void* p) {
        g_cap_armed = 0;
        release_decrypt_ctxs(static_cast<struct shared_ptr*>(p));
    });
    void* kdPtr = (void*)getKdContext(adam, uri, &owner, known, &fetched.p);
    if (!kdPtr || !*(void**)kdPtr) { LOG_WARN("getKdContext failed"); return -1; }
    void* kd = *(void**)kdPtr;
    setup_r1_hook();
    if (!g_r1_hooked) return -1;
    g_cap_armed = 1;
    g_cap_done = 0;
    uint8_t dummy[64] = {0};
    NfcRKVnxuKZy04KWbdFu71Ou(kd, 5, dummy, dummy, 64);
    g_cap_armed = 0;
    release_decrypt_ctxs(&owner);
    context_cleanup.object = nullptr;
    if (!g_cap_done) { LOG_WARN("R1 capture triggered but no data"); return -1; }
    memcpy(cap_ctx, g_cap_ctx, 0x8000);
    memcpy(cap_state, g_cap_state, 0x2100);
    *rcx = g_cap_rcx; *rax = g_cap_rax; *rdx = g_cap_rdx;
    *r9 = g_cap_r9; *rbp = g_cap_rbp;
    return 0;
}

std::string get_m3u8(const std::string& adamId) {
    unsigned long adamID = strtoul(adamId.c_str(), nullptr, 10);
    if (adamID == 0) return "";
    PlaybackLock lock;
    const char* m3u8 = nullptr;
    if (offlineFlag) {
        m3u8 = get_m3u8_download(adamID);
        if (!m3u8) m3u8 = get_m3u8_play(adamID);
    } else {
        m3u8 = get_m3u8_play(adamID);
        if (!m3u8) m3u8 = get_m3u8_download(adamID);
    }
    if (!m3u8) return "";
    std::string result(m3u8);
    free((void*)m3u8);
    return result;
}

M3u8PlaybackResult get_m3u8_playback(const std::string& adamId) {
    M3u8PlaybackResult result;
    const unsigned long adam = strtoul(adamId.c_str(), nullptr, 10);
    if (!adam) return result;

    PlaybackLock lock;
    std::unique_ptr<char, decltype(&free)> url(
        const_cast<char*>(get_m3u8_play(adam, &result.authorization_status)), &free);
    if (url) result.url = url.get();
    return result;
}

std::string get_key(const std::string& adamId, const std::string& uri,
                    uint8_t* ctx, uint8_t* state,
                    uint64_t* rcx, uint64_t* rax, uint64_t* rdx,
                    uint64_t* r9, uint64_t* rbp) {
    PlaybackLock lock;
    persistent_key persistK;
    char* ck = get_content_key_impl(adamId, uri, &persistK.p);
    if (!ck) return "";
    std::string result(ck);
    free(ck);
    if (ctx && state) {
        /* Reusing the persistent key saves a second license round trip, about
           half of each /key. The retry after a refresh fetches a fresh one. */
        if (capture_content_template(adamId, uri, ctx, state, rcx, rax, rdx, r9, rbp, &persistK.p, 0) != 0) {
            LOG_INFO("capture without refresh failed, retry with refresh");
            if (capture_content_template(adamId, uri, ctx, state, rcx, rax, rdx, r9, rbp, nullptr, 1) != 0) {
                LOG_WARN("content template capture failed");
            }
        }
    }
    return result;
}

namespace {
class OwnedNativeContext final : public NativeDecryptContext {
public:
    library_shared_ptr owner;
    void* kd = nullptr;
    bool decrypt(native_batch::Block input, std::string& output, NativeTotals& times) override {
        {
            NativeTimer timer(times.copy_ns);
            output.assign(input.data, input.size);
        }
        {
            ++times.sdk_calls;
            NativeTimer timer(times.sdk_ns);
            NfcRKVnxuKZy04KWbdFu71Ou(kd, 5, &output[0], &output[0], output.size());
        }
        return true;
    }
};

std::unique_ptr<NativeDecryptContext> acquire_native_context(
        const std::string& adam, const std::string& uri) {
    std::unique_ptr<OwnedNativeContext> context(new OwnedNativeContext);
    // Detach the controller's extra references on success AND exceptions.
    // Every active stream retains its own shared_ptr, so clearing the cache
    // cannot destroy another stream's context. Do not return bare kd pointers.
    library_object_guard cache_cleanup(FHinstance, [](void* controller) {
        _ZN21SVFootHillSessionCtrl16resetAllContextsEv(controller);
    });
    persistent_key key;
    std::unique_ptr<char, decltype(&free)> content(
        get_content_key_impl(adam, uri, &key.p), &free);
    if (!content || !key.p.obj) return nullptr;
    _ZN21SVFootHillSessionCtrl14decryptContextERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEERKN11SVDecryptor15SVDecryptorTypeERKb(
        &context->owner.value, FHinstance, static_cast<union std_string*>(key.p.obj));
    if (!context->owner.value.obj) return nullptr;
    auto* kd = _ZNK18SVFootHillPContext9kdContextEv(context->owner.value.obj);
    if (!kd || !*kd) return nullptr;
    context->kd = *static_cast<void**>(*kd);
    if (!context->kd) return nullptr;
    return std::unique_ptr<NativeDecryptContext>(context.release());
}

NativeStreams native_streams;

const char* stream_end_name(NativeStreamEnd end) {
    switch (end) {
    case NativeStreamEnd::Close: return "close";
    case NativeStreamEnd::Idle: return "idle";
    case NativeStreamEnd::Failure: return "failure";
    case NativeStreamEnd::Shutdown: return "shutdown";
    }
    return "unknown";
}

void format_native_totals(char (&out)[384], const NativeTotals& t) {
    snprintf(out, sizeof(out), "requests=%" PRIu64 " bytes=%" PRIu64 " completed_bytes=%" PRIu64
             " sdk_calls=%" PRIu64 " wait_ns=%" PRIu64 " create_ns=%" PRIu64 " copy_ns=%" PRIu64 " sdk_ns=%" PRIu64
             " held_ns=%" PRIu64 " bytes_per_second=%.0f", t.requests, t.bytes, t.completed_bytes,
             t.sdk_calls, t.wait_ns, t.create_ns, t.copy_ns, t.sdk_ns, t.held_ns, t.bytes_per_second());
}

// Declared before the lock guard so even failure summaries are logged after unlocking.
struct StreamReporter {
    NativeReports reports;
    ~StreamReporter() {
        try {
            for (const auto& report : reports) {
                char first[384], reuse[384];
                format_native_totals(first, report.first);
                format_native_totals(reuse, report.reuse);
                LOG_INFO("native_stream_summary end=%s transport_requests=%" PRIu64 " first={%s} reuse={%s}",
                         stream_end_name(report.end), report.transport_requests, first, reuse);
            }
        } catch (...) {} // A diagnostic allocation failure must not change processing.
    }
};

struct BatchReservation {
    const std::string& stream;
    NativeStreams::Batch batch;
    bool active = false;

    BatchReservation(const std::string& stream, uint64_t first_block, size_t count)
        : stream(stream), batch{first_block, count} {}

    ~BatchReservation() {
        if (active) {
            PlaybackLock lock;
            native_streams.release_batch(stream, batch);
        }
    }
};

bool batch_cancelled(const native_batch::Check& cancelled) {
    try { return cancelled && cancelled(); }
    catch (...) { return true; }
}

bool batch_authorized(const native_batch::Check& authorized) {
    try { return authorized && authorized(); }
    catch (...) { return false; }
}
}

static bool valid_native_stream(const std::string& stream, const std::string& adam,
                                const std::string& uri) {
    if (!FHinstance || stream.size() != 32 || adam.empty() || adam.size() > 20 ||
        uri.size() > 2048 || uri.find("skd://itunes.apple.com/") != 0 ||
        (adam != "0" && uri == "skd://itunes.apple.com/P000000000/s1/e1")) return false;
    for (char c : stream) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    for (char c : adam) if (c < '0' || c > '9') return false;
    for (unsigned char c : uri) if (c <= 32 || c == 127) return false;
    return true;
}

static bool valid_native_block(native_batch::Block input) {
    return input.data && input.size && input.size <= (1 << 20) && input.size % 16 == 0;
}

bool native_decrypt(const std::string& stream, const std::string& adam,
                    const std::string& uri, const std::string& input,
                    std::string& output) {
    output.clear();
    const native_batch::Block block{input.data(), input.size()};
    if (!valid_native_block(block) || !valid_native_stream(stream, adam, uri)) return false;
    StreamReporter reporter;
    const auto entered = NativeClock::now();
    PlaybackLock lock;
    const auto acquired = NativeClock::now();
    return native_streams.decrypt(stream, adam, uri, block, output, acquire_native_context,
                                  reporter.reports, entered, acquired, acquired) == native_batch::Status::Complete;
}

void native_batch::enable() {
    native_batch_enabled.store(true, std::memory_order_relaxed);
}

native_batch::Result native_batch::decrypt(const std::string& stream, const std::string& adam,
                                          const std::string& uri, uint64_t first_block,
                                          const Block* blocks, size_t count,
                                          const Check& authorized, const Check& cancelled) {
    Result result;
    if (!native_batch_enabled.load(std::memory_order_relaxed)) return result;
    result.status = Status::Invalid;
    if (!blocks || !count || count > max_blocks || first_block > UINT64_MAX - count ||
        !valid_native_stream(stream, adam, uri)) return result;
    size_t bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!valid_native_block(blocks[i]) || blocks[i].size > max_bytes - bytes) return result;
        bytes += blocks[i].size;
    }

    result.output.reserve(count);
    StreamReporter reporter;
    BatchReservation reservation(stream, first_block, count);
    for (size_t i = 0; i < count; ++i) {
        if (batch_cancelled(cancelled)) {
            result.status = Status::Cancelled;
            break;
        }
        if (!batch_authorized(authorized)) {
            result.status = Status::Unauthorized;
            break;
        }
        std::string output;
        // Reserve writable storage before waiting for the SDK turn.
        output.reserve(blocks[i].size);
        {
            const auto entered = NativeClock::now();
            auto turn = take_playback_turn();
            if (batch_cancelled(cancelled)) {
                result.status = Status::Cancelled;
                break;
            }
            std::lock_guard<std::mutex> lock(g_playback_mutex);
            const auto acquired = NativeClock::now();
            try {
                result.status = native_streams.decrypt(stream, adam, uri, blocks[i], output,
                    acquire_native_context, reporter.reports, entered, acquired, acquired, &reservation.batch, i);
                if (result.status == Status::Complete) reservation.active = i + 1 < count;
            } catch (...) {
                result.status = Status::BlockFailed;
            }
        }
        if (result.status != Status::Complete) break;
        result.output.push_back(std::move(output));
    }
    return result;
}

void native_decrypt_close(const std::string& stream) {
    StreamReporter reporter;
    PlaybackLock lock;
    native_streams.close(stream, reporter.reports);
}

void native_decrypt_expire() {
    StreamReporter reporter;
    PlaybackLock lock;
    native_streams.expire(reporter.reports);
}

void native_decrypt_close_all() {
    StreamReporter reporter;
    PlaybackLock lock;
    native_streams.clear(reporter.reports);
}
