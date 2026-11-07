#pragma once

#include "auth_policy.h"
#include "import.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>

// CoreFoundation types are opaque. CFRetainedPointer<T> has an indirect return
// containing one retained pointer in this Android x86_64 ABI.
extern "C" {
void _ZN17storeservicescore11URLResponse18protocolDictionaryEv(void*, void*);
const void* CFStringCreateWithCString(const void*, const char*, uint32_t);
const void* CFDictionaryGetValue(const void*, const void*);
unsigned long CFGetTypeID(const void*);
unsigned long CFNumberGetTypeID();
unsigned long CFStringGetTypeID();
unsigned char CFNumberGetValue(const void*, int, void*);
unsigned char CFStringGetCString(const void*, char*, long, uint32_t);
void CFRelease(const void*);
}

inline AuthNumber auth_protocol_number(const void* dictionary, const char* name) {
    AuthNumber result;
    const void* key = CFStringCreateWithCString(nullptr, name, 0x08000100); // UTF-8
    if (!key) return result;
    const void* value = CFDictionaryGetValue(dictionary, key);
    CFRelease(key);
    if (!value) return result;
    if (CFGetTypeID(value) == CFNumberGetTypeID()) {
        int64_t number = 0;
        if (CFNumberGetValue(value, 4, &number)) result.set(number); // SInt64
    } else if (CFGetTypeID(value) == CFStringGetTypeID()) {
        char text[32] = {};
        if (CFStringGetCString(value, text, sizeof(text), 0x08000100)) {
            const char* p = text;
            if (*p == '-') ++p;
            if (*p && strspn(p, "0123456789") == strlen(p)) {
                char* end = nullptr;
                errno = 0;
                const long long number = strtoll(text, &end, 10);
                if (!errno && end && !*end) result.set(number);
            }
        }
    }
    return result;
}

inline AuthDiagnostics inspect_auth_response(void* response, struct shared_ptr* url_response) {
    AuthDiagnostics d;
    if (response) {
        d.response_type.set(_ZNK17storeservicescore20AuthenticateResponse12responseTypeEv(response));
        const int* status = _ZNK17storeservicescore20AuthenticateResponse6statusEv(response);
        if (status) d.status.set(*status);
        auto* error = _ZNK17storeservicescore20AuthenticateResponse5errorEv(response);
        if (error && error->obj) {
            const int* code = _ZNK17storeservicescore19StoreErrorCondition9errorCodeEv(error->obj);
            const long* external = _ZNK17storeservicescore19StoreErrorCondition17externalErrorCodeEv(error->obj);
            if (code) d.store_error.set(*code);
            if (external) d.failure_type.set(*external);
        }
    }
    if (!url_response || !url_response->obj) return d;
    const void* dictionary = nullptr;
    _ZN17storeservicescore11URLResponse18protocolDictionaryEv(&dictionary, url_response->obj);
    if (dictionary) {
        // Do not substitute StoreErrorCondition::errorCode for this field.
        d.protocol_error = auth_protocol_number(dictionary, "errorCode");
        CFRelease(dictionary);
    }
    auto* http = _ZNK17storeservicescore11URLResponse18underlyingResponseEv(url_response->obj);
    if (http && http->obj) {
        // Bundled mediaplatform::HTTPResponse extends the 128-byte HTTPMessage
        // with an int status at +128. responseWithStatus and the guest ABI test
        // verify this offset; AuthenticateResponse::status is a different enum.
        int status = 0;
        memcpy(&status, static_cast<const char*>(http->obj) + 128, sizeof(status));
        if (status >= 100 && status <= 599) d.http_status.set(status);
    }
    return d;
}
