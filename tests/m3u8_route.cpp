// Exercise Lite Main helpers without starting the service or contacting Apple.
#define main wrapper_lite_main
#include "../lite/lite_main.cpp"
#undef main
#include <cassert>

static int legacy_calls = 0;
static int playback_calls = 0;
static M3u8PlaybackResult playback_fixture;

std::string get_m3u8(const std::string& adamId) {
    assert(adamId == "123");
    ++legacy_calls;
    return "https://example.invalid/legacy";
}

M3u8PlaybackResult get_m3u8_playback(const std::string& adamId) {
    assert(adamId == "123");
    ++playback_calls;
    return playback_fixture;
}

static void check(const httplib::Params& params, int code, const char* expected_url = nullptr) {
    httplib::Request request;
    request.params = params;
    httplib::Response response;
    handle_m3u8(request, response);
    // httplib defaults a handled response with no explicit status to HTTP 200.
    assert(response.status == ((code == 401 || code == 403) ? code : -1));
    assert(response.get_header_value("Content-Type") == "application/json");
    cJSON* root = cJSON_Parse(response.body.c_str());
    assert(root);
    const auto* response_code = cJSON_GetObjectItemCaseSensitive(root, "code");
    assert(cJSON_IsNumber(response_code) && response_code->valueint == code);
    const auto* data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (expected_url) {
        assert(cJSON_IsObject(data));
        assert(std::string(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, "adamId"))) == "123");
        assert(std::string(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, "m3u8"))) == expected_url);
    } else {
        assert(!data);
    }
    cJSON_Delete(root);
}

static void check_login_cache_readback() {
    char directory[] = "/tmp/wrapper-cache-readback-XXXXXX";
    assert(mkdtemp(directory));
    const auto sf = std::string(directory) + "/STOREFRONT_ID";
    const auto dev = std::string(directory) + "/DEV_TOKEN";
    assert(read_login_token_file(dev, TokenStage::DeveloperTokenFile).empty());
    assert(replace_token_cache_file(sf, "000000", 6) == TokenFileResult::Ok);
    assert(read_login_token_file(sf, TokenStage::StorefrontFile).empty());
    const std::string us = "143441-1,29\r\n";
    assert(replace_token_cache_file(sf, us.data(), us.size()) == TokenFileResult::Ok);
    assert(read_login_token_file(sf, TokenStage::StorefrontFile) == "us");
    assert(remove(sf.c_str()) == 0 && rmdir(directory) == 0);
}

int main() {
    check({{"adamId", "123"}}, 0, "https://example.invalid/legacy");
    check({{"adamId", "123"}, {"source", "download"}}, 400);
    check({{"adamId", "123"}, {"source", ""}}, 400);
    check({{"adamId", "123"}, {"source", "playback"}, {"source", "download"}}, 400);
    check({{"source", "playback"}}, 400);
    assert(legacy_calls == 1 && playback_calls == 0);

    playback_fixture.url = "https://example.invalid/opaque?signature=fixture";
    const httplib::Params playback = {{"adamId", "123"}, {"source", "playback"}};
    check(playback, 0, playback_fixture.url.c_str());
    playback_fixture.url.clear();
    check(playback, 404);
    for (int code : {401, 403}) {
        playback_fixture.authorization_status = code;
        check(playback, code);
    }
    assert(legacy_calls == 1 && playback_calls == 4);
    puts("M3U8 route: legacy default, parameter validation, playback JSON and denial status passed");
    check_login_cache_readback();
    puts("Login cache readback: missing dev token, invalid and normalized storefront passed");
}
