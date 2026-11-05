// Android/NDK test, run inside the same guest as lite. No account/network needed.
#include "../lite/tokens.cpp"
#include "../lite/apple_api.cpp"
#include <cassert>
#include <memory>

bool g_ssl_verify_disabled = false;
extern "C" void _ZN13mediaplatform4DataC1EPKvmb(void*, const void*, size_t, bool);
extern "C" void _ZN13mediaplatform4DataD2Ev(void*);

int main() {
    static_assert(sizeof(std::string) == sizeof(union std_string), "string ABI");
    static_assert(sizeof(std::shared_ptr<void>) == sizeof(struct shared_ptr), "shared_ptr ABI");
    {
        alignas(std::max_align_t) char data[64] = {};
        const char bytes[] = {'a', 'b', 'c'};
        _ZN13mediaplatform4DataC1EPKvmb(data, bytes, sizeof(bytes), true);
        library_object_guard guard(data, _ZN13mediaplatform4DataD2Ev);
        std::string copied;
        assert(read_library_data(data, 3, copied) && copied == "abc");
    }
    {
        CurlEasy curl;
        assert(curl.ok && curl.setOptInt(CURLOPT_POSTFIELDSIZE, 3));
        assert(curl.getResponseCode() == 0); // valid handle, no HTTP request yet
    }
    for (int i = 0; i < 10000; ++i) {
        library_shared_ptr message;
        message.value = make_http_message("https://example.test/a/long/url", "GET");
        std::shared_ptr<void> native;
        // Transfer the single owning reference to a real NDK shared_ptr.
        memcpy(&native, &message.value, sizeof(message.value));
        message.value = {nullptr, nullptr};
        std::weak_ptr<void> weak(native);
        native.reset();
        assert(weak.expired());

        library_string text;
        new (&text.value) std::string(1024, 'x');
        assert(strlen(text.data()) == 1024);

        auto owner = std::make_shared<int>(42);
        {
            void* storage = ::operator new(sizeof(std::shared_ptr<int>));
            new (storage) std::shared_ptr<int>(owner);
            assert(owner.use_count() == 2);
            library_shared_vector vector({storage, static_cast<char*>(storage) + sizeof(struct shared_ptr),
                                         static_cast<char*>(storage) + sizeof(struct shared_ptr)});
        }
        assert(owner.use_count() == 1);
    }
    puts("10000 Android ABI ownership cycles passed");
}
