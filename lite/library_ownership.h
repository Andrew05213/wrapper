#pragma once

#include <cstdint>
#include <cstring>
#include <sys/types.h>
#include <new>
#include "import.h"

/* Only wrap owning return values. Accessors returning pointers to members are
   borrowed and must stay alive through their parent object instead. */
struct library_shared_ptr {
    struct shared_ptr value = {nullptr, nullptr};
    library_shared_ptr() = default;
    library_shared_ptr(const library_shared_ptr&) = delete;
    library_shared_ptr& operator=(const library_shared_ptr&) = delete;
    void retain(const struct shared_ptr* borrowed) {
        if (!borrowed) return;
        value = *borrowed;
        if (value.ctrl_blk)
            __atomic_fetch_add(reinterpret_cast<long*>(value.ctrl_blk) + 1, 1L, __ATOMIC_RELAXED);
    }
    ~library_shared_ptr() {
        if (value.ctrl_blk)
            _ZNSt6__ndk119__shared_weak_count16__release_sharedEv(value.ctrl_blk);
    }
};

/* These strings are constructed by the Android library, unlike the borrowed
   input views made by new_std_string(). Use the matching libc++ destructor. */
struct library_string {
    union std_string value = {};
    library_string() = default;
    library_string(const library_string&) = delete;
    library_string& operator=(const library_string&) = delete;
    ~library_string() {
        _ZNSt6__ndk112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEED2Ev(&value);
    }
    const char* data() { return std_string_data(&value); }
};

struct library_shared_vector {
    struct std_vector value;
    explicit library_shared_vector(struct std_vector v) : value(v) {}
    library_shared_vector(const library_shared_vector&) = delete;
    library_shared_vector& operator=(const library_shared_vector&) = delete;
    ~library_shared_vector() {
        auto* begin = static_cast<struct shared_ptr*>(value.begin);
        auto* end = static_cast<struct shared_ptr*>(value.end);
        while (end != begin) {
            --end;
            if (end->ctrl_blk)
                _ZNSt6__ndk119__shared_weak_count16__release_sharedEv(end->ctrl_blk);
        }
        ::operator delete(value.begin);
    }
};

/* Register after a successful constructor; storage belongs to the caller. */
struct library_object_guard {
    void* object;
    void (*destroy)(void*);
    library_object_guard(void* p, void (*d)(void*)) : object(p), destroy(d) {}
    library_object_guard(const library_object_guard&) = delete;
    library_object_guard& operator=(const library_object_guard&) = delete;
    ~library_object_guard() { if (object) destroy(object); }
};
