#include "../lite/apple_api.cpp"
#include <cassert>
#include <sys/stat.h>
#include <unistd.h>

bool g_ssl_verify_disabled = false;

int main() {
    Logger::get().configure(LogLevel::Error, "", 0);
    char directory[] = "/tmp/wrapper-cache-XXXXXX";
    assert(mkdtemp(directory));
    TokenCache cache;
    cache.base_dir = directory;
    cache.dev_token = "fixture-dev"; cache.music_token = "fixture-music";
    cache.storefront_id = "us";
    assert(cache.save());
    TokenCache loaded; loaded.base_dir = directory; loaded.load();
    assert(loaded.dev_token == cache.dev_token && loaded.music_token == cache.music_token);
    const std::string file = std::string(directory) + "/token_cache.json";
    const std::string temporary = file + ".tmp";
    // Failed writes must report failure and preserve the previous cache.
    assert(mkdir(temporary.c_str(), 0700) == 0);
    cache.music_token = "new-token";
    assert(!cache.save());
    loaded.music_token.clear(); loaded.load();
    assert(loaded.music_token == "fixture-music");
    rmdir(temporary.c_str()); remove(file.c_str()); rmdir(directory);
    cache.base_dir = std::string(directory) + "/missing";
    assert(!cache.save());
    puts("token cache: persistence failure is explicit and preserves the old cache");
}
