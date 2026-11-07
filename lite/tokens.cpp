#include "internal.h"
#include "library_ownership.h"
#include <cstddef>
#include <memory>
#include "cJSON.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <mutex>
#include <sys/time.h>
#include <unistd.h>
#include <sys/wait.h>

static std::string read_token_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    while (!content.empty() && (content.back() == ' ' || content.back() == '\n' ||
                                 content.back() == '\r')) content.pop_back();
    return content;
}

std::string get_storefront() {
    return read_token_file(std::string(g_base_dir) + "/STOREFRONT_ID");
}

std::string get_music_token() {
    return read_token_file(std::string(g_base_dir) + "/MUSIC_TOKEN");
}

long long getCurrentTimeMillis() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

// Data::bytes() is a bounded byte buffer, not a C string.
static bool read_library_data(void* data, size_t maximum, std::string& out) {
    if (!data) return false;
    size_t size = _ZNK13mediaplatform4Data6lengthEv(data);
    const char* bytes = _ZNK13mediaplatform4Data5bytesEv(data);
    if (!bytes || !size || size > maximum) return false;
    out.assign(bytes, size);
    return true;
}

static char* get_guid() {
    library_shared_ptr data;
    _ZN17storeservicescore10DeviceGUID4guidEv(&data.value, GUID.obj);
    std::string guid;
    if (!read_library_data(data.value.obj, 4096, guid)) return nullptr;
    if (guid.find('\0') != std::string::npos) return nullptr;
    return strdup(guid.c_str());
}

// Every response accessor below borrows storage from the live URLRequest.
static char* response_token(void* request, const char* name) {
    auto* error = _ZNK17storeservicescore10URLRequest5errorEv(request);
    if (!error || error->obj) return nullptr;
    auto* response = _ZNK17storeservicescore10URLRequest8responseEv(request);
    if (!response || !response->obj) return nullptr;
    auto* underlying = _ZNK17storeservicescore11URLResponse18underlyingResponseEv(response->obj);
    if (!underlying || !underlying->obj) return nullptr;
    void* data = *reinterpret_cast<void**>(static_cast<char*>(underlying->obj) + 48);
    std::string body;
    if (!read_library_data(data, 256 * 1024, body)) return nullptr;
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLength(body.data(), body.size()), &cJSON_Delete);
    const cJSON* token = cJSON_GetObjectItemCaseSensitive(root.get(), name);
    if (!cJSON_IsString(token) || !token->valuestring[0]) return nullptr;
    return strdup(token->valuestring);
}

/* Match the bundled libc++ emplace layout: vptr, two zero-based counts,
   then HTTPMessage at +24. A count of 0 represents one owning reference.
   Artificially pinning either count at 1 keeps every request alive forever. */
static struct shared_ptr make_http_message(const char* url, const char* method) {
    library_object_guard allocation(::operator new(512), [](void* p) { ::operator delete(p); });
    auto* ptr = static_cast<uint8_t*>(allocation.object);
    memset(ptr, 0, 512);
    *(void**)ptr = &_ZTVNSt6__ndk120__shared_ptr_emplaceIN13mediaplatform11HTTPMessageENS_9allocatorIS2_EEEE + 2;
    struct shared_ptr httpMessage = {.obj = ptr + 24, .ctrl_blk = ptr};
    union std_string u = new_std_string(url);
    union std_string m = new_std_string(method);
    _ZN13mediaplatform11HTTPMessageC2ENSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES7_(httpMessage.obj, &u, &m);
    allocation.object = nullptr;
    return httpMessage;
}

static char* get_dev_token_impl() {
    library_shared_ptr message_owner;
    message_owner.value = make_http_message("https://sf-api-token-service.itunes.apple.com/apiToken", "GET");
    struct shared_ptr& httpMessage = message_owner.value;

    /* URLRequest is larger than 512 bytes; a 512-byte buffer overflowed in
       the constructor and corrupted the caller's stack frame (crashing the
       refresh with a null std::string move).  2048 bytes keeps the object
       contained. */
    alignas(std::max_align_t) uint8_t urlRequest[2048];
    memset(urlRequest, 0, sizeof(urlRequest));
    _ZN17storeservicescore10URLRequestC2ERKNSt6__ndk110shared_ptrIN13mediaplatform11HTTPMessageEEERKNS2_INS_14RequestContextEEE(urlRequest, &httpMessage, &g_reqCtx);
    library_object_guard request_owner(urlRequest, _ZN17storeservicescore10URLRequestD2Ev);
    union std_string clientIdName = new_std_string("clientId");
    union std_string clientIdValue = new_std_string("musicAndroid");
    _ZN17storeservicescore10URLRequest19setRequestParameterERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(urlRequest, &clientIdName, &clientIdValue);
    union std_string versionName = new_std_string("version");
    union std_string versionValue = new_std_string("1");
    _ZN17storeservicescore10URLRequest19setRequestParameterERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(urlRequest, &versionName, &versionValue);
    _ZN17storeservicescore10URLRequest3runEv(urlRequest);

    return response_token(urlRequest, "token");
}

static char* get_music_user_token_impl(const char* authToken, const std::string& storefront) {
    std::unique_ptr<char, decltype(&free)> guid(get_guid(), &free);
    if (!guid) return nullptr;

    library_shared_ptr message_owner;
    message_owner.value = make_http_message("https://play.itunes.apple.com/WebObjects/MZPlay.woa/wa/createMusicToken", "POST");
    struct shared_ptr& httpMessage = message_owner.value;

    union std_string ctHeader = new_std_string("Content-Type");
    union std_string ctValue = new_std_string("application/json; charset=UTF-8");
    _ZN13mediaplatform11HTTPMessage9setHeaderERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(httpMessage.obj, &ctHeader, &ctValue);
    union std_string expectHeader = new_std_string("Expect");
    union std_string expectValue = new_std_string("");
    _ZN13mediaplatform11HTTPMessage9setHeaderERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(httpMessage.obj, &expectHeader, &expectValue);
    union std_string bundleIdHeader = new_std_string("X-Apple-Requesting-Bundle-Id");
    union std_string bundleIdValue = new_std_string("com.apple.android.music");
    _ZN13mediaplatform11HTTPMessage9setHeaderERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(httpMessage.obj, &bundleIdHeader, &bundleIdValue);
    union std_string bundleVerHeader = new_std_string("X-Apple-Requesting-Bundle-Version");
    library_string bundle_version;
    _ZNK17storeservicescore14RequestContext9userAgentEv(&bundle_version.value, g_reqCtx.obj);
    _ZN13mediaplatform11HTTPMessage9setHeaderERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(httpMessage.obj, &bundleVerHeader, &bundle_version.value);
    union std_string storefront_header = new_std_string("X-Apple-Store-Front");
    union std_string storefront_value = new_std_string(storefront.c_str());
    _ZN13mediaplatform11HTTPMessage9setHeaderERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(httpMessage.obj, &storefront_header, &storefront_value);

    cJSON* bodyJson = cJSON_CreateObject();
    cJSON_AddStringToObject(bodyJson, "guid", guid.get());
    cJSON_AddStringToObject(bodyJson, "assertion", authToken ? authToken : "");
    char tcc[32];
    snprintf(tcc, sizeof(tcc), "%lld", (long long)getCurrentTimeMillis());
    cJSON_AddStringToObject(bodyJson, "tcc-acceptance-date", tcc);
    char* body = cJSON_PrintUnformatted(bodyJson);
    cJSON_Delete(bodyJson);
    if (body) {
        _ZN13mediaplatform11HTTPMessage11setBodyDataEPcm(httpMessage.obj, body, strlen(body));
        cJSON_free(body);
    }

    /* URLRequest is larger than 512 bytes; see get_dev_token_impl(). */
    alignas(std::max_align_t) uint8_t urlRequest[2048];
    memset(urlRequest, 0, sizeof(urlRequest));
    _ZN17storeservicescore10URLRequestC2ERKNSt6__ndk110shared_ptrIN13mediaplatform11HTTPMessageEEERKNS2_INS_14RequestContextEEE(urlRequest, &httpMessage, &g_reqCtx);
    library_object_guard request_owner(urlRequest, _ZN17storeservicescore10URLRequestD2Ev);
    _ZN17storeservicescore10URLRequest3runEv(urlRequest);

    return response_token(urlRequest, "music_token");
}

static char* get_account_storefront_id_impl() {
    library_string region;
    struct shared_ptr urlbag = {.obj = nullptr, .ctrl_blk = nullptr};
    _ZNK17storeservicescore14RequestContext20storeFrontIdentifierERKNSt6__ndk110shared_ptrINS_6URLBagEEE(&region.value, g_reqCtx.obj, &urlbag);
    return region.data() ? strdup(region.data()) : nullptr;
}

int offline_available_impl() {
    struct shared_ptr* fairplay = (struct shared_ptr*)malloc(16);
    memset(fairplay, 0, 16);
    _ZN17storeservicescore14RequestContext8fairPlayEv(fairplay, g_reqCtx.obj);
    if (!fairplay->obj) {
        free(fairplay);
        return 0;
    }
    struct std_vector fairplay_status = _ZN17storeservicescore8FairPlay21getSubscriptionStatusEv(fairplay->obj);
    char* begin_ptr = (char*)fairplay_status.begin;
    char* end_ptr = (char*)fairplay_status.end;
    free(fairplay);
    if (!begin_ptr || !end_ptr || (end_ptr - begin_ptr) < 32) return 0;
    char* second_item_ptr = begin_ptr + 16;
    int state = *(int*)((char*)second_item_ptr + 8);
    if (state == 2 || state == 3) return 1;
    return 0;
}

static bool write_token_file(const std::string& path, const std::string& content) {
    std::string tmp = path + ".tmp";
    FILE* fp = fopen(tmp.c_str(), "w");
    if (!fp) {
        LOG_WARN("token_file_write_failed");
        return false;
    }
    fwrite(content.data(), 1, content.size(), fp);
    fclose(fp);
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        LOG_WARN("token_file_replace_failed");
        remove(tmp.c_str());
        return false;
    }
    return true;
}

bool refresh_tokens(std::string& out_storefront, std::string& out_dev_token, std::string& out_music_token) {
    std::lock_guard<std::mutex> lock(g_token_mutex);

    char* storefront = get_account_storefront_id_impl();
    if (!storefront) {
        LOG_WARN("failed to get storefront ID");
        return false;
    }
    std::string sf(storefront);
    free(storefront);

    char* devToken = get_dev_token_impl();
    if (!devToken) {
        LOG_WARN("failed to get dev token");
        return false;
    }
    write_token_file(std::string(g_base_dir) + "/DEV_TOKEN", std::string(devToken));

    char* musicToken = get_music_user_token_impl(devToken, sf);
    std::string music;
    if (musicToken) {
        music = musicToken;
        free(musicToken);
    } else {
        music = read_token_file(std::string(g_base_dir) + "/MUSIC_TOKEN");
        if (!music.empty()) {
            LOG_WARN("music_token_cache_used");
        }
    }

    if (music.empty()) {
        LOG_WARN("failed to get music token");
        return false;
    }

    write_token_file(std::string(g_base_dir) + "/STOREFRONT_ID", sf);
    write_token_file(std::string(g_base_dir) + "/MUSIC_TOKEN", music);

    std::string freshDev = read_token_file(std::string(g_base_dir) + "/DEV_TOKEN");
    out_storefront = std::move(sf);
    out_dev_token = std::move(freshDev);
    out_music_token = std::move(music);
    return true;
}

bool cache_login_tokens() {
    /* Login already succeeded, so a token fetch failure is most likely
       transient; retry a few times before treating the login as failed. */
    static const int kAttempts = 5;
    std::string sf, dev, music;
    for (int attempt = 1; attempt <= kAttempts; ++attempt) {
        if (refresh_tokens(sf, dev, music)) {
            g_tokens.storefront_id = sf;
            g_tokens.dev_token = dev;
            g_tokens.music_token = music;
            LOG_INFO("account info cached successfully");
            return true;
        }
        LOG_WARN("token fetch failed after login (attempt %d/%d)", attempt, kAttempts);
        if (attempt < kAttempts) sleep(2);
    }
    LOG_ERROR("failed to cache account info after %d attempts", kAttempts);
    return false;
}

std::string fetch_dev_token() {
    std::lock_guard<std::mutex> lock(g_token_mutex);
    char* token = get_dev_token_impl();
    if (!token) return "";
    std::string result(token);
    free(token);
    return result;
}

std::string get_dev_token() { return g_tokens.dev_token; }
