// NDK-only, run in the QEMU guest. No account or network requests are used.
#include "../lite/auth.cpp"
#include "../lite/bridge.cpp"
#include "../lite/tokens.cpp"
#include <cassert>
#include <cstddef>

extern "C" {
void _ZN13mediaplatform11HTTPMessage18responseWithStatusEi(void*, void*, int);
void _ZN17storeservicescore11URLResponseC1ENSt6__ndk110shared_ptrIN13mediaplatform11HTTPMessageEEENS2_INS3_12HTTPResponseEEE(void*, struct shared_ptr*, struct shared_ptr*);
int _ZN17storeservicescore16AuthenticateFlow20_resolveEmptyAccountEv(void*);
bool _ZN17storeservicescore16AuthenticateFlow34_shouldRetryAfterIncorrectPasswordERKNSt6__ndk110shared_ptrINS_20AuthenticateResponseEEE(void*, struct shared_ptr*);
void _ZN17storeservicescore20AuthenticateResponse18setCustomerMessageERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void*, union std_string*);
void _ZN17storeservicescore19AuthenticateRequestC1ERKNSt6__ndk110shared_ptrINS_14RequestContextEEE(void*, struct shared_ptr*);
void _ZN17storeservicescore19AuthenticateRequestD2Ev(void*);
void _ZN17storeservicescore19AuthenticateRequest11setPasswordERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void*, union std_string*);
void _ZN17storeservicescore19AuthenticateRequest11setUserNameERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void*, union std_string*);
void _ZN17storeservicescore19AuthenticateRequest16setAttemptNumberEi(void*, int);
void _ZN17storeservicescore19AuthenticateRequest12_makeMessageERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void*, void*, union std_string*);
void _ZNK17storeservicescore14RequestContext9userAgentEv(void*, void*);
void _ZN17storeservicescore14RequestContext16deviceIdentifierEv(void*, void*);
void _ZNK17storeservicescore14RequestContext13cookieStorageEv(void*, void*);
const void* CFDataCreate(const void*, const unsigned char*, long);
const void* CFPropertyListCreateWithData(const void*, const void*, unsigned long, void*, void*);
}

static std::string plist_string(const void* dictionary, const char* name) {
    const void* key = CFStringCreateWithCString(nullptr, name, 0x08000100);
    const void* value = CFDictionaryGetValue(dictionary, key);
    CFRelease(key);
    assert(value && CFGetTypeID(value) == CFStringGetTypeID());
    char buffer[20000] = {};
    assert(CFStringGetCString(value, buffer, sizeof(buffer), 0x08000100));
    return buffer;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    Logger::get().configure(LogLevel::Debug, "", 0);
    puts("AUTH_ABI_START");
    install_hooks();
    set_base_dir("/data/auth-offline-fixture");
    init("Music/5.0.2/Android/10/Pixel 8/7663314/en-US/en-US/e82320052964d21a");
    g_reqCtx = init_ctx();
    puts("AUTH_ABI_CONTEXT");
    assert(!g_ssl_verify_disabled);
    // Initial account-choice dialog is local and translated by the library.
    library_shared_ptr flow;
    _ZNSt6__ndk110shared_ptrIN17storeservicescore16AuthenticateFlowEE11make_sharedIJRNS0_INS1_14RequestContextEEEEEES3_DpOT_(&flow.value, &g_reqCtx);
    assert(_ZN17storeservicescore16AuthenticateFlow20_resolveEmptyAccountEv(flow.value.obj) == 2);

    puts("AUTH_ABI_DIALOG");
    std::string agent, identifier;
    _ZNK17storeservicescore14RequestContext9userAgentEv(&agent, g_reqCtx.obj);
    _ZN17storeservicescore14RequestContext16deviceIdentifierEv(&identifier, g_reqCtx.obj);
    assert(strstr(agent.c_str(), "Music/5.0.2"));
    assert(strstr(agent.c_str(), "Android/10"));
    assert(strstr(agent.c_str(), "Pixel 8"));
    puts("AUTH_ABI_DEVICE");
    const std::string original_identifier = identifier.c_str();
    assert(!original_identifier.empty());
    library_shared_ptr cookies;
    _ZNK17storeservicescore14RequestContext13cookieStorageEv(&cookies.value, g_reqCtx.obj);
    assert(cookies.value.obj);
    void* original_cookies = cookies.value.obj;

    for (const auto& password : {std::string(" \tP:a\\ss\"&<密码>\r\n "), std::string(4096, 'x')}) {
        alignas(std::max_align_t) char request[2048] = {};
        _ZN17storeservicescore19AuthenticateRequestC1ERKNSt6__ndk110shared_ptrINS_14RequestContextEEE(request, &g_reqCtx);
        library_object_guard owner(request, _ZN17storeservicescore19AuthenticateRequestD2Ev);
        auto user = new_std_string("fixture@example.invalid");
        auto pass = new_std_string(password.c_str());
        _ZN17storeservicescore19AuthenticateRequest11setUserNameERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(request, &user);
        _ZN17storeservicescore19AuthenticateRequest11setPasswordERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(request, &pass);
        for (int attempt : {0, 1}) {
            _ZN17storeservicescore19AuthenticateRequest16setAttemptNumberEi(request, attempt);
            library_shared_ptr message;
            auto url = new_std_string("https://buy.itunes.apple.com/WebObjects/MZFinance.woa/wa/authenticate");
            _ZN17storeservicescore19AuthenticateRequest12_makeMessageERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(&message.value, request, &url);
            assert(message.value.obj);
            assert(strcmp(std_string_data(static_cast<union std_string*>(message.value.obj)), std_string_data(&url)) == 0);
            assert(strcmp(std_string_data(reinterpret_cast<union std_string*>(static_cast<char*>(message.value.obj) + 24)), "POST") == 0);
            void* data = *reinterpret_cast<void**>(static_cast<char*>(message.value.obj) + 48);
            std::string body;
            assert(read_library_data(data, 32768, body));
            const void* cf_data = CFDataCreate(nullptr, reinterpret_cast<const unsigned char*>(body.data()), body.size());
            const void* dictionary = CFPropertyListCreateWithData(nullptr, cf_data, 0, nullptr, nullptr);
            assert(dictionary);
            assert(plist_string(dictionary, "appleId") == "fixture@example.invalid");
            assert(plist_string(dictionary, "password") == password);
            assert(plist_string(dictionary, "attempt") == std::to_string(attempt));
            assert(plist_string(dictionary, "guid") == original_identifier);
            CFRelease(dictionary); CFRelease(cf_data);
        }
    }
    library_shared_ptr cookies_after;
    _ZNK17storeservicescore14RequestContext13cookieStorageEv(&cookies_after.value, g_reqCtx.obj);
    assert(cookies_after.value.obj == original_cookies);

    puts("AUTH_ABI_REQUESTS");
    for (const auto& code : {std::string("-5000"), std::string("-22947")}) {
        library_shared_ptr message, http;
        message.value = make_http_message("https://example.invalid/authenticate", "POST");
        _ZN13mediaplatform11HTTPMessage18responseWithStatusEi(&http.value, message.value.obj, 200);
        const std::string xml = "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
            "<key>failureType</key><string>-5000</string><key>errorCode</key><string>" + code +
            "</string></dict></plist>";
        auto header = new_std_string("Content-Type"), content = new_std_string("text/xml");
        _ZN13mediaplatform11HTTPMessage9setHeaderERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_(http.value.obj, &header, &content);
        _ZN13mediaplatform11HTTPMessage11setBodyDataEPcm(http.value.obj, const_cast<char*>(xml.data()), xml.size());
        alignas(std::max_align_t) char url_storage[256] = {}, response_storage[128] = {};
        _ZN17storeservicescore11URLResponseC1ENSt6__ndk110shared_ptrIN13mediaplatform11HTTPMessageEEENS2_INS3_12HTTPResponseEEE(url_storage, &message.value, &http.value);
        struct shared_ptr url = {url_storage, nullptr};
        _ZN17storeservicescore20AuthenticateResponseC1ERKNSt6__ndk110shared_ptrINS_11URLResponseEEE(response_storage, &url);
        const auto d = inspect_auth_response(response_storage, &url);
        assert(d.store_error.present && d.store_error.value == 10);
        assert(d.failure_type.present && d.failure_type.value == -5000);
        assert(d.protocol_error.present && d.protocol_error.value == std::stoll(code));
        assert(d.http_status.present && d.http_status.value == 200);
        assert(d.status.present && d.status.value == 6);
        assert(d.response_type.value == (code == "-22947" ? 1 : 0));
        assert(classify_auth_response(d) == (code == "-22947" ? AuthOutcome::VerificationRequired : AuthOutcome::AuthenticationFailed));
        if (code == "-5000") {
            auto secret = new_std_string("SECRET-server-prompt-must-not-be-logged");
            _ZN17storeservicescore20AuthenticateResponse18setCustomerMessageERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(response_storage, &secret);
            { std::lock_guard<std::mutex> lock(auth_state_mutex); auth_active = true; }
            auth_response_observed(response_storage, &url);
            struct shared_ptr response = {response_storage, nullptr};
            assert(!_ZN17storeservicescore16AuthenticateFlow34_shouldRetryAfterIncorrectPasswordERKNSt6__ndk110shared_ptrINS_20AuthenticateResponseEEE(flow.value.obj, &response));
            assert(last_auth_outcome() == AuthOutcome::AuthenticationFailed);
            { std::lock_guard<std::mutex> lock(auth_state_mutex); auth_active = false; }
        }
        // These destructors are inlined in the library; release the verified
        // member layout of these fixture objects explicitly.
        for (int offset : {8, 56})
            reinterpret_cast<std::string*>(response_storage + offset)->~basic_string();
        for (int offset : {32, 80}) {
            auto* p = reinterpret_cast<struct shared_ptr*>(response_storage + offset);
            if (p->ctrl_blk) _ZNSt6__ndk119__shared_weak_count16__release_sharedEv(p->ctrl_blk);
        }
        for (int offset : {0, 16, 40}) {
            auto* p = reinterpret_cast<struct shared_ptr*>(url_storage + offset);
            if (p->ctrl_blk) _ZNSt6__ndk119__shared_weak_count16__release_sharedEv(p->ctrl_blk);
        }
    }
    puts("AUTH_ABI_PASSED: native dialog, request credentials/POST/attempt/GUID/cookies/UA, protocol and HTTP fields");
}
