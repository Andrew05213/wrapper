// Data fixtures deliberately have no trailing NUL byte.
#include "../lite/tokens.cpp"
#include <cassert>

struct shared_ptr GUID = {};
struct shared_ptr g_reqCtx = {};
struct Fixture { const char* bytes; size_t length; };
static Fixture data;
static int releases = 0;
static struct shared_ptr error = {}, response = {}, underlying = {};
static bool no_response = false;
static bool no_fairplay = false, throw_status = false;
static size_t status_count = 2;
static int subscription_state = 2;
alignas(void*) static char message[64] = {};
extern "C" char* _ZNK13mediaplatform4Data5bytesEv(void* p) { return const_cast<char*>(static_cast<Fixture*>(p)->bytes); }
extern "C" size_t _ZNK13mediaplatform4Data6lengthEv(void* p) { return static_cast<Fixture*>(p)->length; }
extern "C" void* _ZN17storeservicescore10DeviceGUID4guidEv(void* p, void*) {
    *static_cast<struct shared_ptr*>(p) = {&data, &data}; return p;
}
extern "C" void _ZNSt6__ndk119__shared_weak_count16__release_sharedEv(void*) { ++releases; }
extern "C" struct shared_ptr* _ZNK17storeservicescore10URLRequest5errorEv(void*) { return &error; }
extern "C" struct shared_ptr* _ZNK17storeservicescore10URLRequest8responseEv(void*) { return no_response ? nullptr : &response; }
extern "C" struct shared_ptr* _ZNK17storeservicescore11URLResponse18underlyingResponseEv(void*) { return &underlying; }
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
    puts("token data: bounded GUID/JSON, long tokens, missing/error responses passed");
}
