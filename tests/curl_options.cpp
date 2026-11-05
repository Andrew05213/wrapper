#include "../lite/apple_api.cpp"
#include <cassert>
bool g_ssl_verify_disabled = false;

int main(int argc, char** argv) {
    assert(argc == 3);
    g_ssl_verify_disabled = argv[1][0] == '1';
    CurlEasy curl;
    assert(curl.ok);
    assert(curl.setOptInt(CURLOPT_TIMEOUT_MS, 3000));
    assert(curl.setOptInt(CURLOPT_POSTFIELDSIZE, 3));
    const char body[4] = {'a', 0, 'b', 0};
    assert(curl.setOptStr(CURLOPT_POSTFIELDS, body));
    assert(curl.setOptStr(CURLOPT_URL, argv[2]));
    assert(curl.setOptStr(10004, "")); // no proxy for this loopback fixture
    std::string response;
    assert(curl.setOptPtr(CURLOPT_WRITEFUNCTION, (void*)&writeCallback));
    assert(curl.setOptPtr(CURLOPT_WRITEDATA, &response));
    bool performed = curl.perform();
    printf("%d %ld %s\n", performed, curl.getResponseCode(), response.c_str());
}
