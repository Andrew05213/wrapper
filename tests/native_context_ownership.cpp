#include "native_fake_sdk.h"

int main() {
    Logger::get().configure(LogLevel::Warn, "", 0);
    const std::string a(32, 'a'), b(32, 'b'), sample(16, 'x');
    const std::string uri = "skd://itunes.apple.com/content";
    std::string out;
    for (int i = 0; i < 2048; ++i) {
        assert(native_decrypt(a, "1", uri, sample, out));
        assert(keys == 0 && contexts == 1 && cache.empty() && out.size() == sample.size() && out[0] == (sample[0] ^ 1));
        assert(native_decrypt(b, "2", uri, sample, out));
        assert(contexts == 2 && cache.empty());
        native_decrypt_close(a);
        assert(contexts == 1);
        assert(native_decrypt(b, "2", uri, sample, out));
        native_decrypt_close(b);
        assert(contexts == 0 && keys == 0);
    }
    fail_context = true;
    try { native_decrypt(a, "1", uri, sample, out); assert(false); }
    catch (const std::runtime_error&) {}
    assert(live.empty() && cache.empty());
    fail_context = false;
    throw_sample = true;
    try { native_decrypt(a, "1", uri, sample, out); assert(false); }
    catch (const std::runtime_error&) {}
    assert(live.empty() && cache.empty());
    throw_sample = false;
    assert(!native_decrypt(a, "1", "skd://itunes.apple.com/P000000000/s1/e1", sample, out));
    assert(!native_decrypt(a, "1", uri, "bad size", out));
    assert(!native_decrypt("bad stream", "1", uri, sample, out));
    assert(!native_decrypt(a, "1", uri + std::string(1, '\0') + "suffix", sample, out));
    assert(native_decrypt(a, "1", uri, sample, out));
    native_decrypt_close_all();
    assert(live.empty() && cache.empty());
    puts("native bridge: 4096 stream lifetimes, cache detachment and exceptions passed");
}
