#include "internal.h"
#include "dobby.h"
#include "cJSON.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <dlfcn.h>
#include <mutex>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include <functional>

void* FHinstance = nullptr;
struct shared_ptr apInf;
struct shared_ptr GUID;
struct shared_ptr g_reqCtx;
std::mutex g_playback_mutex;
std::mutex g_token_mutex;
char* amUsername = nullptr;
char* amPassword = nullptr;
char* device_infos[9];
char g_base_dir[256] = "data";
int offlineFlag = 0;
uint8_t leaseMgr[256];
bool g_ssl_verify_disabled = false;
bool g_code_from_file = false;

extern const char* const fairplayCert;
extern void* _ZTVN17storeservicescore14DialogHandlerE;
extern void* _ZTVN17storeservicescore24AuthenticationChallengeHandlerE;

int file_exists(const char* path) {
    FILE* f = fopen(path, "r");
    if (f) { fclose(f); return 1; }
    return 0;
}

static void split_string_safe(const char* input, const char* delim, char** out, int max, char** copy) {
    *copy = strdup(input);
    if (!*copy) return;
    char* saveptr;
    char* token = strtok_r(*copy, delim, &saveptr);
    int i = 0;
    while (token && i < max) {
        out[i++] = token;
        token = strtok_r(nullptr, delim, &saveptr);
    }
}

void init(const char* device_info_str) {
    LOG_INFO("initializing...");
    static char* prev_copy = nullptr;
    if (prev_copy) free(prev_copy);
    char* copy = nullptr;
    split_string_safe(device_info_str, "/", device_infos, 9, &copy);
    prev_copy = copy;

    setenv("ANDROID_DNS_MODE", "local", 1);
    setenv("ANDROID_DATA", "/data", 1);
    setenv("ANDROID_ROOT", "/system", 1);
    static const char* resolvers[2] = {"223.5.5.5", "223.6.6.6"};
    _resolv_set_nameservers_for_net(0, resolvers, 2, ".");

    union std_string conf1 = new_std_string(device_infos[8] ? device_infos[8] : "");
    union std_string conf2 = new_std_string("");
    _ZN14FootHillConfig6configERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEE(&conf1);

    _ZN17storeservicescore10DeviceGUID8instanceEv(&GUID);
    static uint8_t ret[88];
    static unsigned int conf3 = 29;
    static uint8_t conf4 = 1;
    _ZN17storeservicescore10DeviceGUID9configureERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_RKjRKb(
        &ret, GUID.obj, &conf1, &conf2, &conf3, &conf4);
}

struct shared_ptr init_ctx() {
    LOG_INFO("initializing ctx...");
    LOG_DEBUG("request_context_initializing");
    std::string mplDbPath = std::string(g_base_dir) + "/mpl_db";
    union std_string strBuf = new_std_string(mplDbPath.c_str());

    struct shared_ptr reqCtx;
    memset(&reqCtx, 0, sizeof(reqCtx));
    _ZNSt6__ndk110shared_ptrIN17storeservicescore14RequestContextEE11make_sharedIJRNS_12basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEEEEES3_DpOT_(
        &reqCtx, &strBuf);

    static uint8_t ptr[480];
    memset(ptr, 0, sizeof(ptr));
    *(void**)ptr = &_ZTVNSt6__ndk120__shared_ptr_emplaceIN17storeservicescore20RequestContextConfigENS_9allocatorIS2_EEEE + 2;
    struct shared_ptr reqCtxCfg = {.obj = ptr + 32, .ctrl_blk = ptr};
    _ZN17storeservicescore20RequestContextConfigC2Ev(reqCtxCfg.obj);

    _ZN17storeservicescore20RequestContextConfig20setBaseDirectoryPathERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[0] ? device_infos[0] : "0");
    _ZN17storeservicescore20RequestContextConfig19setClientIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[1] ? device_infos[1] : "0");
    _ZN17storeservicescore20RequestContextConfig20setVersionIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[2] ? device_infos[2] : "0");
    _ZN17storeservicescore20RequestContextConfig21setPlatformIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[3] ? device_infos[3] : "0");
    _ZN17storeservicescore20RequestContextConfig17setProductVersionERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[4] ? device_infos[4] : "0");
    _ZN17storeservicescore20RequestContextConfig14setDeviceModelERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[5] ? device_infos[5] : "0");
    _ZN17storeservicescore20RequestContextConfig15setBuildVersionERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[6] ? device_infos[6] : "0");
    _ZN17storeservicescore20RequestContextConfig19setLocaleIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(reqCtxCfg.obj, &strBuf);
    strBuf = new_std_string(device_infos[7] ? device_infos[7] : "0");
    _ZN17storeservicescore20RequestContextConfig21setLanguageIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(reqCtxCfg.obj, &strBuf);

    _ZN21RequestContextManager9configureERKNSt6__ndk110shared_ptrIN17storeservicescore14RequestContextEEE(&reqCtx);
    static uint8_t buf[88];
    _ZN17storeservicescore14RequestContext4initERKNSt6__ndk110shared_ptrINS_20RequestContextConfigEEE(&buf, reqCtx.obj, &reqCtxCfg);

    strBuf = new_std_string(g_base_dir);
    _ZN17storeservicescore14RequestContext24setFairPlayDirectoryPathERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(reqCtx.obj, &strBuf);

    _ZNSt6__ndk110shared_ptrIN20androidstoreservices28AndroidPresentationInterfaceEE11make_sharedIJEEES3_DpOT_(&apInf);
    _ZN20androidstoreservices28AndroidPresentationInterface16setDialogHandlerEPFvlNSt6__ndk110shared_ptrIN17storeservicescore14ProtocolDialogEEENS2_INS_36AndroidProtocolDialogResponseHandlerEEEE(apInf.obj, &dialogHandler);
    _ZN20androidstoreservices28AndroidPresentationInterface21setCredentialsHandlerEPFvNSt6__ndk110shared_ptrIN17storeservicescore18CredentialsRequestEEENS2_INS_33AndroidCredentialsResponseHandlerEEEE(apInf.obj, &credentialHandler);
    _ZN17storeservicescore14RequestContext24setPresentationInterfaceERKNSt6__ndk110shared_ptrINS_21PresentationInterfaceEEE(reqCtx.obj, &apInf);

    return reqCtx;
}

static int (*orig_debug_log_enabled)(void);
static int (*orig_log_print)(int prio, const char *tag, const char *fmt, ...);
static int (*orig_log_write)(int prio, const char *tag, const char *text);
static int (*orig_curl_easy_setopt)(void *curl, int32_t option, ...);

static uint8_t allDebug() { return 0; }

int log_print_hook(int prio, const char *tag, const char *fmt, ...) {
    // Library messages can contain credentials, headers and whole responses.
    // Never format or forward them, even at wrapper's debug log level.
    (void)prio; (void)tag; (void)fmt;
    return 0;
}

int log_write_hook(int prio, const char *tag, const char *text) {
    (void)prio; (void)tag; (void)text;
    return 0;
}

int curl_easy_setopt_hook(void *curl, int32_t option, ...) {
    va_list args;
    va_start(args, option);
    void* param = va_arg(args, void*);
    va_end(args);
    if (g_ssl_verify_disabled && (option == 64 || option == 81 || option == 10230)) {
        LOG_DEBUG("hooked curl_easy_setopt %d (SSL verify disabled)", option);
        return orig_curl_easy_setopt(curl, option, 0L);
    }
    return orig_curl_easy_setopt(curl, option, param);
}

static void (*original_auth_response)(void*, struct shared_ptr*);
static int (*original_account_choice)(void*);
static bool (*original_failure_prompt)(void*, struct shared_ptr*);
struct AuthDialogScope {
    AuthDialogOrigin previous;
    explicit AuthDialogScope(AuthDialogOrigin origin) : previous(set_auth_dialog_origin(origin)) {}
    ~AuthDialogScope() { set_auth_dialog_origin(previous); }
};
static int account_choice_hook(void* flow) {
    AuthDialogScope scope(AuthDialogOrigin::AccountChoice);
    return original_account_choice(flow);
}
static bool failure_prompt_hook(void* flow, struct shared_ptr* response) {
    AuthDialogScope scope(AuthDialogOrigin::FailurePrompt);
    return original_failure_prompt(flow, response);
}
static void auth_response_hook(void* response, struct shared_ptr* url_response) {
    original_auth_response(response, url_response);
    // Observation only: never alter URL, method, body, headers or cookies.
    auth_response_observed(response, url_response);
}

void install_hooks() {
    static int installed = 0;
    if (installed) return;
    installed = 1;
    DobbyHook((void*)&_ZN13mediaplatform26DebugLogEnabledForPriorityENS_11LogPriorityE,
              (void*)&allDebug, (void**)&orig_debug_log_enabled);
    DobbyHook((void*)&__android_log_print, (void*)&log_print_hook, (void**)&orig_log_print);
    DobbyHook((void*)&__android_log_write, (void*)&log_write_hook, (void**)&orig_log_write);
    if (DobbyHook((void*)&_ZN17storeservicescore16AuthenticateFlow20_resolveEmptyAccountEv,
                  (void*)&account_choice_hook, (void**)&original_account_choice) != 0)
        LOG_WARN("auth_dialog_classifier_unavailable");
    if (DobbyHook((void*)&_ZN17storeservicescore16AuthenticateFlow34_shouldRetryAfterIncorrectPasswordERKNSt6__ndk110shared_ptrINS_20AuthenticateResponseEEE,
                  (void*)&failure_prompt_hook, (void**)&original_failure_prompt) != 0)
        LOG_WARN("auth_failure_prompt_classifier_unavailable");
    if (DobbyHook((void*)&_ZN17storeservicescore20AuthenticateResponseC1ERKNSt6__ndk110shared_ptrINS_11URLResponseEEE,
                  (void*)&auth_response_hook, (void**)&original_auth_response) != 0)
        LOG_WARN("auth_response_observer_unavailable");
    if (g_ssl_verify_disabled) {
        DobbyHook((void*)&curl_easy_setopt, (void*)&curl_easy_setopt_hook, (void**)&orig_curl_easy_setopt);
    }
    LOG_DEBUG("debug hooks installed");
}

void set_base_dir(const char* dir) {
    snprintf(g_base_dir, sizeof(g_base_dir), "%s", dir ? dir : "data");
}

void set_device_info(const char* device_info) {
    init(device_info);
}

static void endLeaseCb(const int&) { LOG_INFO("end lease"); }
static void pbErrCb(std::shared_ptr<void>) { LOG_INFO("playback error"); }
extern "C" std::function<void(const int&)> endLeaseCallback(endLeaseCb);
extern "C" std::function<void(std::shared_ptr<void>)> pbErrCallback(pbErrCb);

void setup_services() {
    _ZN22SVPlaybackLeaseManagerC2ERKNSt6__ndk18functionIFvRKiEEERKNS1_IFvRKNS0_10shared_ptrIN17storeservicescore19StoreErrorConditionEEEEEE(
        leaseMgr, &endLeaseCallback, &pbErrCallback);
    uint8_t autom = 1;
    _ZN22SVPlaybackLeaseManager25refreshLeaseAutomaticallyERKb(leaseMgr, &autom);
    _ZN22SVPlaybackLeaseManager12requestLeaseERKb(leaseMgr, &autom);
    FHinstance = _ZN21SVFootHillSessionCtrl8instanceEv();
    offlineFlag = offline_available_impl();
    if (offlineFlag) LOG_INFO("This account supports offline channel");
}
