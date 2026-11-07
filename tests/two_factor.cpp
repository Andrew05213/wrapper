#include "../lite/auth.cpp"
#include <cassert>
#include <sys/stat.h>

char* amUsername = nullptr;
char* amPassword = nullptr;
char g_base_dir[256] = {};
bool g_code_from_file = true;
struct shared_ptr apInf = {};
void* _ZTVNSt6__ndk120__shared_ptr_emplaceIN17storeservicescore19CredentialsResponseENS_9allocatorIS2_EEEE = nullptr;
static std::string submitted;
static int response_type;
static bool requires_code = true;
static std::vector<AuthEvent> events;
static void record_event(const AuthEvent& event, void*) {
    (void)last_auth_outcome();
    set_auth_event_handler(record_event);
    events.push_back(event);
    assert(!event.outcome_present && !event.diagnostics.response_type.present);
}
int file_exists(const char* path) { struct stat info; return stat(path, &info) == 0; }
extern "C" uint8_t _ZNK17storeservicescore18CredentialsRequest28requiresHSA2VerificationCodeEv(void*) { return requires_code; }
extern "C" union std_string* _ZNK17storeservicescore18CredentialsRequest5titleEv(void*) {
    static union std_string empty = new_std_string(""); return &empty;
}
extern "C" union std_string* _ZNK17storeservicescore18CredentialsRequest7messageEv(void* p) {
    return _ZNK17storeservicescore18CredentialsRequest5titleEv(p);
}
extern "C" void _ZN17storeservicescore19CredentialsResponseC1Ev(void*) { submitted.clear(); }
extern "C" void _ZN17storeservicescore19CredentialsResponse11setUserNameERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void*, union std_string*) {}
extern "C" void _ZN17storeservicescore19CredentialsResponse11setPasswordERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void*, union std_string* p) { submitted = std_string_data(p); }
extern "C" void _ZN17storeservicescore19CredentialsResponse15setResponseTypeENS0_12ResponseTypeE(void*, int type) { response_type = type; }
extern "C" void _ZN20androidstoreservices28AndroidPresentationInterface25handleCredentialsResponseERKNSt6__ndk110shared_ptrIN17storeservicescore19CredentialsResponseEEE(void*, struct shared_ptr*) {}
extern "C" void _ZNSt6__ndk119__shared_weak_count16__release_sharedEv(void* p) { free(p); }
int main() {
    Logger::get().configure(LogLevel::Error, "", 0);
    set_auth_event_handler(record_event);
    char directory[] = "/tmp/wrapper-2fa-XXXXXX";
    assert(mkdtemp(directory));
    snprintf(g_base_dir, sizeof(g_base_dir), "%s", directory);
    const std::string path = std::string(directory) + "/2fa.txt";
    struct shared_ptr request = {&submitted, nullptr};
    for (size_t length : {8u, 58u, 64u, 4096u}) {
        const std::string password(length, 'x');
        for (const std::string code : {"123456", "1234567", "abcdef"}) {
            set_credentials("fixture@example.invalid", password.c_str());
            FILE* fp = fopen(path.c_str(), "w"); assert(fp);
            fputs(code.c_str(), fp); fclose(fp);
            events.clear();
            credentialHandler(&request, nullptr);
            assert(events.size() == 2 && events[0].type == AuthEventType::CredentialsRequested);
            assert(events[1].type == AuthEventType::VerificationCodeRequested);
            assert(submitted == (code == "123456" ? password + code : ""));
            assert(response_type == (code == "123456" ? 2 : 0));
            assert(std::string(amPassword) == password);
            assert(!file_exists(path.c_str()));
        }
    }
    const std::string special = " \tP:a\\ss\"&<密码>\r ";
    set_credentials("fixture@example.invalid", special.c_str());
    for (const char* code : {"123456", "987654"}) {
        FILE* fp = fopen(path.c_str(), "w"); assert(fp);
        fputs(code, fp); fclose(fp);
        credentialHandler(&request, nullptr);
        assert(submitted == special + code);
        assert(std::string(amPassword) == special);
    }
    set_verification_code_provider(+[](std::string& code) {
        assert(events.back().type == AuthEventType::VerificationCodeRequested);
        code = "111111"; return true;
    });
    credentialHandler(&request, nullptr);
    assert(submitted == special + "111111");
    set_verification_code_provider(nullptr);
    requires_code = false;
    credentialHandler(&request, nullptr);
    assert(submitted == special); // Ordinary retry must not contain a prior code.
    set_credentials(nullptr, nullptr);
    credentialHandler(&request, nullptr);
    assert(submitted.empty()); // service reauthentication must not exit/wait for a file
    rmdir(directory);
    assert(response_type == 0);
    puts("2FA: original/special/long passwords, repeated challenges and cancellation passed");
}
