// Data fixtures deliberately have no trailing NUL byte.
#include "../lite/tokens.cpp"
#include "../lite/apple_api.cpp"
#include <cassert>
#include <sys/stat.h>

bool g_ssl_verify_disabled = false;
struct shared_ptr GUID = {};
struct shared_ptr g_reqCtx = {};
std::mutex g_token_mutex;
char g_base_dir[256];
struct Fixture { const char* bytes; size_t length; };
static Fixture data;
static int releases = 0;
static struct shared_ptr error = {}, response = {}, underlying = {};
static bool no_response = false;
static bool no_fairplay = false, throw_status = false;
static size_t status_count = 2;
static int subscription_state = 2;
alignas(void*) static char message[144] = {};
static bool bad_storefront = false, throw_developer = false;
static int developer_failures = 0, storefront_calls = 0, developer_calls = 0, music_calls = 0;
static bool bad_music = false;
static const char* secret_developer = "SENTINEL_DEVELOPER";
static const char* secret_music = "SENTINEL_MUSIC";
static const char* secret_cached = "SENTINEL_CACHED";
static std::string response_body;
void* _ZTVNSt6__ndk120__shared_ptr_emplaceIN13mediaplatform11HTTPMessageENS_9allocatorIS2_EEEE;
extern "C" char* _ZNK13mediaplatform4Data5bytesEv(void* p) { return const_cast<char*>(static_cast<Fixture*>(p)->bytes); }
extern "C" size_t _ZNK13mediaplatform4Data6lengthEv(void* p) { return static_cast<Fixture*>(p)->length; }
extern "C" void* _ZN17storeservicescore10DeviceGUID4guidEv(void* p, void*) {
    *static_cast<struct shared_ptr*>(p) = {&data, &data}; return p;
}
extern "C" void _ZNSt6__ndk119__shared_weak_count16__release_sharedEv(void* p) {
    ++releases;
    if (p != &data) ::operator delete(p);
}
extern "C" struct shared_ptr* _ZNK17storeservicescore10URLRequest5errorEv(void*) { return &error; }
extern "C" struct shared_ptr* _ZNK17storeservicescore10URLRequest8responseEv(void*) { return no_response ? nullptr : &response; }
extern "C" struct shared_ptr* _ZNK17storeservicescore11URLResponse18underlyingResponseEv(void*) { return &underlying; }

extern "C" union std_string* _ZNK17storeservicescore14RequestContext20storeFrontIdentifierERKNSt6__ndk110shared_ptrINS_6URLBagEEE(
    void* out, void*, struct shared_ptr*) {
    ++storefront_calls;
    auto* value = static_cast<union std_string*>(out);
    *value = new_std_string("143441-1,29");
    if (bad_storefront) {
        *value = {};
        value->mark = 1; // Long-mode result with no native data.
    }
    return value;
}
extern "C" void _ZNSt6__ndk112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEED2Ev(union std_string*) {}
extern "C" void _ZNK17storeservicescore14RequestContext9userAgentEv(void*, void*) {}
extern "C" void* _ZN13mediaplatform11HTTPMessageC2ENSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES7_(
    void* p, union std_string*, union std_string* method) {
    *static_cast<char*>(p) = *std_string_data(method);
    return p;
}
extern "C" void _ZN13mediaplatform11HTTPMessage9setHeaderERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(
    void*, union std_string*, union std_string*) {}
extern "C" void _ZN13mediaplatform11HTTPMessage11setBodyDataEPcm(void*, char* body, u_long) {
    assert(strstr(body, secret_developer));
}
extern "C" void* _ZN17storeservicescore10URLRequestC2ERKNSt6__ndk110shared_ptrIN13mediaplatform11HTTPMessageEEERKNS2_INS_14RequestContextEEE(
    void* p, struct shared_ptr* source, struct shared_ptr*) {
    *static_cast<char*>(p) = *static_cast<char*>(source->obj);
    return p;
}
extern "C" void _ZN17storeservicescore10URLRequestD2Ev(void*) {}
extern "C" void* _ZN17storeservicescore10URLRequest19setRequestParameterERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(
    void* p, union std_string*, union std_string*) { return p; }
extern "C" void* _ZN17storeservicescore10URLRequest3runEv(void* p) {
    const bool is_dev = *static_cast<char*>(p) == 'G';
    int status = 200;
    error.obj = nullptr;
    if (is_dev) {
        ++developer_calls;
        if (throw_developer) throw std::runtime_error("SENTINEL_EXCEPTION");
        if (developer_failures > 0) {
            --developer_failures;
            status = 503;
            error.obj = &error;
        }
    } else {
        ++music_calls;
    }
    if (error.obj || (!is_dev && bad_music)) {
        response_body = R"({"error":"SENTINEL_RESPONSE","url":"https://fixture.invalid/SENTINEL_URL","cookie":"SENTINEL_COOKIE","authorization":"SENTINEL_HEADER","account":"SENTINEL_ACCOUNT","password":"SENTINEL_PASSWORD","code":"SENTINEL_CODE"})";
    } else {
        response_body = is_dev ? std::string("{\"token\":\"") + secret_developer + "\"}" :
                                 std::string("{\"music_token\":\"") + secret_music + "\"}";
    }
    data = {response_body.data(), response_body.size()};
    memcpy(message + 128, &status, sizeof(status));
    return p;
}
extern "C" void* _ZN17storeservicescore14RequestContext8fairPlayEv(void* out, void*) {
    *static_cast<struct shared_ptr*>(out) = {no_fairplay ? nullptr : &data, &data};
    return out;
}
extern "C" struct std_vector _ZN17storeservicescore8FairPlay21getSubscriptionStatusEv(void*) {
    if (throw_status) throw std::runtime_error("fixture_status_error");
    auto* storage = static_cast<char*>(::operator new(status_count * 16));
    memset(storage, 0, status_count * 16);
    if (status_count >= 2) memcpy(storage + 24, &subscription_state, sizeof(subscription_state));
    return {storage, storage + status_count * 16, storage + status_count * 16};
}

class Capture {
public:
    explicit Capture(FILE* stream)
        : stream_(stream), saved_(dup(fileno(stream))), file_(tmpfile()) {
        assert(saved_ >= 0 && file_);
        fflush(stream_);
        assert(dup2(fileno(file_), fileno(stream_)) >= 0);
    }
    ~Capture() { if (file_) finish(); }

    std::string finish() {
        fflush(stream_);
        assert(dup2(saved_, fileno(stream_)) >= 0);
        close(saved_);
        rewind(file_);
        std::string result;
        char buffer[4096];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), file_)) != 0) result.append(buffer, n);
        fclose(file_);
        file_ = nullptr;
        return result;
    }
private:
    FILE* stream_;
    int saved_;
    FILE* file_;
};

static void check_login_token_diagnostics() {
    Capture out(stdout), err(stderr);
    char directory[] = "/tmp/wrapper-login-tokens-XXXXXX";
    assert(mkdtemp(directory));
    snprintf(g_base_dir, sizeof(g_base_dir), "%s", directory);
    const std::string dir(directory);
    const auto dev_file = dir + "/DEV_TOKEN";
    const auto music_file = dir + "/MUSIC_TOKEN";
    const auto sf_file = dir + "/STOREFRONT_ID";
    const auto json_file = dir + "/token_cache.json";
    no_response = false;
    std::string sf, dev, music;

    bad_storefront = true;
    assert(!refresh_tokens_impl(sf, dev, music, 1));
    assert(developer_calls == 0 && music_calls == 0);
    bad_storefront = false;
    developer_failures = 1;
    assert(!refresh_tokens_impl(sf, dev, music, 1));
    assert(music_calls == 0);
    throw_developer = true;
    const int before_throw = developer_calls;
    assert(!cache_login_tokens() && developer_calls == before_throw + 1);
    throw_developer = false;

    // HTTP 200 with no token is still a fetch failure.
    bad_music = true;
    assert(!refresh_tokens_impl(sf, dev, music, 1));
    const char invalid[] = "SENTINEL_INVALID\0cache";
    assert(replace_token_cache_file(music_file, invalid, sizeof(invalid) - 1) == TokenFileResult::Ok);
    assert(!refresh_tokens_impl(sf, dev, music, 2));
    assert(replace_token_cache_file(music_file, secret_cached, strlen(secret_cached)) == TokenFileResult::Ok);
    assert(refresh_tokens_impl(sf, dev, music, 3));
    assert(music == secret_cached);
    bad_music = false;

    // Raw writes remain nonterminal. The JSON cache reports terminal write and replace failures.
    assert(mkdir((dev_file + ".tmp").c_str(), 0700) == 0);
    assert(remove(sf_file.c_str()) == 0 && mkdir(sf_file.c_str(), 0700) == 0);
    assert(refresh_tokens_impl(sf, dev, music, 4));
    assert(dev == secret_developer); // Failed raw write left the existing file in place.
    assert(rmdir((dev_file + ".tmp").c_str()) == 0 && rmdir(sf_file.c_str()) == 0);
    TokenCache cache;
    cache.base_dir = dir;
    cache.dev_token = secret_developer;
    cache.music_token = secret_cached;
    assert(mkdir((json_file + ".tmp").c_str(), 0700) == 0);
    assert(!cache.save(true));
    assert(rmdir((json_file + ".tmp").c_str()) == 0);
    assert(mkdir(json_file.c_str(), 0700) == 0);
    assert(!cache.save(true));
    assert(rmdir(json_file.c_str()) == 0);
    assert(cache.save(true));

    developer_failures = 1;
    const int before_success = storefront_calls;
    assert(cache_login_tokens() && storefront_calls == before_success + 2);
    bad_music = true;
    assert(remove(music_file.c_str()) == 0);
    const int before_exhaustion = storefront_calls;
    assert(!cache_login_tokens() && storefront_calls == before_exhaustion + 5);

    const std::string events = out.finish();
    const std::string logs = err.finish();
    assert(events.find("SENTINEL") == std::string::npos && logs.find("SENTINEL") == std::string::npos);
    assert(events.find(directory) == std::string::npos && logs.find(directory) == std::string::npos);
    const std::string prefix = "WRAPPER_TOKEN_EVENT:";
    std::istringstream lines(events);
    std::string line;
    while (std::getline(lines, line)) {
        assert(line.compare(0, prefix.size(), prefix) == 0);
        std::unique_ptr<cJSON, decltype(&cJSON_Delete)> record(
            cJSON_ParseWithOpts(line.c_str() + prefix.size(), nullptr, 1), &cJSON_Delete);
        assert(cJSON_IsObject(record.get()));
        assert(strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(record.get(), "type")),
                      "login_token_diagnostic") == 0);
        for (auto* field = record->child; field; field = field->next) {
            const std::string key = field->string;
            if (key == "type" || key == "stage" || key == "reason") assert(cJSON_IsString(field));
            else if (key == "usedCache") assert(cJSON_IsBool(field));
            else if (key == "attempt" || key == "httpStatus") assert(cJSON_IsNumber(field));
            else assert(false);
        }
    }
    auto expect = [&](const char* stage, const char* reason, unsigned attempt, bool used, int status = 0) {
        const std::string fields = std::string("\"stage\":\"") + stage + "\",\"reason\":\"" + reason +
            "\",\"attempt\":" + std::to_string(attempt) + ",\"usedCache\":" + (used ? "true" : "false") +
            (status ? ",\"httpStatus\":" + std::to_string(status) : "") + "}";
        assert(events.find(fields) != std::string::npos);
        assert(logs.find(fields) != std::string::npos);
    };
    expect("storefront", "storefront_fetch_failed", 1, false);
    expect("developer_token", "developer_token_fetch_failed", 1, false, 503);
    expect("developer_token", "developer_token_fetch_failed", 1, false); // exception, no observed status
    expect("music_user_token", "music_user_token_fetch_failed", 1, false, 200);
    expect("music_token_fallback", "cache_read_failed", 1, false);
    expect("music_token_fallback", "cache_read_failed", 5, false);
    expect("music_token_fallback", "cache_invalid", 2, false);
    expect("music_token_fallback", "cache_fallback_used", 3, true, 200);
    expect("developer_token_file", "cache_write_failed", 4, false);
    expect("storefront_file", "cache_replace_failed", 4, false);
    expect("token_cache_json", "cache_write_failed", 0, false);
    expect("token_cache_json", "cache_replace_failed", 0, false);
    expect("refresh", "retry_succeeded", 2, false);
    expect("refresh", "retry_exhausted", 5, false);
    for (const auto& file : {dev_file, sf_file, json_file}) assert(remove(file.c_str()) == 0);
    assert(rmdir(directory) == 0);
}

int main() {
    const char guid[3] = {'a', 'b', 'c'};
    data = {guid, sizeof(guid)};
    char* copied = get_guid();
    assert(copied && strcmp(copied, "abc") == 0 && releases == 1);
    free(copied);
    const char embedded[3] = {'a', 0, 'b'};
    data = {embedded, sizeof(embedded)};
    assert(!get_guid() && releases == 2);
    data = {guid, 4097};
    assert(!get_guid() && releases == 3); // reject before reading the bytes
    std::string raw;
    assert(!read_library_data(nullptr, 4096, raw));

    response.obj = &response;
    underlying.obj = message;
    *reinterpret_cast<void**>(message + 48) = &data;
    std::string body = "{\"token\" : \"" + std::string(5000, 'x') + "\"}";
    data = {body.data(), body.size()};
    copied = response_token(nullptr, "token");
    assert(copied && strlen(copied) == 5000);
    free(copied);
    assert(!response_token(nullptr, "music_token"));
    body = "{\"music_token\":\"good\"}";
    data = {body.data(), body.size()};
    copied = response_token(nullptr, "music_token");
    assert(copied && strcmp(copied, "good") == 0);
    free(copied);
    const char invalid[2] = {'{', 'x'};
    data = {invalid, 2};
    assert(!response_token(nullptr, "token"));
    data = {invalid, 256 * 1024 + 1};
    assert(!response_token(nullptr, "token"));
    no_response = true;
    assert(!response_token(nullptr, "token"));
    for (int state : {0, 1, 2, 3, 4}) {
        subscription_state = state;
        const int before = releases;
        assert(offline_available_impl() == (state == 2 || state == 3));
        assert(releases == before + 1);
    }
    for (size_t count : {0u, 1u}) {
        status_count = count;
        const int before = releases;
        assert(!offline_available_impl() && releases == before + 1);
    }
    no_fairplay = true;
    int before = releases;
    assert(!offline_available_impl() && releases == before + 1);
    no_fairplay = false; throw_status = true;
    before = releases;
    try { offline_available_impl(); assert(false); }
    catch (const std::runtime_error&) { assert(releases == before + 1); }
    check_login_token_diagnostics();
    puts("login tokens: acquisition, fallback, cache I/O, retries and redaction passed");
    puts("token data: bounded GUID/JSON, long tokens, missing/error responses passed");
}
