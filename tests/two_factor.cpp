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
int file_exists(const char* path) { struct stat info; return stat(path, &info) == 0; }
extern "C" uint8_t _ZNK17storeservicescore18CredentialsRequest28requiresHSA2VerificationCodeEv(void*) { return 1; }
extern "C" union std_string* _ZNK17storeservicescore18CredentialsRequest5titleEv(void*) {
    static union std_string empty = new_std_string(""); return &empty;
}
extern "C" union std_string* _ZNK17storeservicescore18CredentialsRequest7messageEv(void* p) {
    return _ZNK17storeservicescore18CredentialsRequest5titleEv(p);
}
extern "C" void _ZN17storeservicescore19CredentialsResponseC1Ev(void*) {}
extern "C" void _ZN17storeservicescore19CredentialsResponse11setUserNameERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void*, union std_string*) {}
extern "C" void _ZN17storeservicescore19CredentialsResponse11setPasswordERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(void*, union std_string* p) { submitted = std_string_data(p); }
extern "C" void _ZN17storeservicescore19CredentialsResponse15setResponseTypeENS0_12ResponseTypeE(void*, int) {}
extern "C" void _ZN20androidstoreservices28AndroidPresentationInterface25handleCredentialsResponseERKNSt6__ndk110shared_ptrIN17storeservicescore19CredentialsResponseEEE(void*, struct shared_ptr* p) {
    free(p->ctrl_blk); // fixture owns the opaque response after submission
}
int main() {
    Logger::get().configure(LogLevel::Error, "", 0);
    char directory[] = "/tmp/wrapper-2fa-XXXXXX";
    assert(mkdtemp(directory));
    snprintf(g_base_dir, sizeof(g_base_dir), "%s", directory);
    const std::string path = std::string(directory) + "/2fa.txt";
    struct shared_ptr request = {};
    for (size_t length : {8u, 58u, 64u, 4096u}) {
        const std::string password(length, 'x');
        for (const std::string code : {"123456", "1234567", "abcdef"}) {
            set_credentials("fixture@example.invalid", password.c_str());
            FILE* fp = fopen(path.c_str(), "w"); assert(fp);
            fputs(code.c_str(), fp); fclose(fp);
            credentialHandler(&request, nullptr);
            assert(submitted == password + (code == "123456" ? code : ""));
            assert(!file_exists(path.c_str()));
        }
    }
    set_credentials(nullptr, nullptr);
    credentialHandler(&request, nullptr);
    assert(submitted.empty()); // service reauthentication must not exit/wait for a file
    rmdir(directory);
    puts("2FA: 58/64/4096-byte passwords, invalid codes and missing credentials passed");
}
