#include "../lite/auth_input.h"
#include <cassert>
#include <sstream>

static FILE* record(const std::string& text) {
    FILE* fp = tmpfile(); assert(fp);
    assert(fwrite(text.data(), 1, text.size(), fp) == text.size());
    rewind(fp); return fp;
}

int main() {
    for (const auto& password : {std::string(" \t:\"'\\&<密码>\r\n "), std::string(4096, 'x'),
                                  std::string("literal\\u0000")}) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "appleEmail", "fixture@example.invalid");
        cJSON_AddStringToObject(root, "applePassword", password.c_str());
        char* printed = cJSON_PrintUnformatted(root);
        FILE* fp = record(std::string(printed) + "\n");
        std::string user, actual;
        assert(auth_input::read_credentials(fp, user, actual));
        assert(actual == password && user == "fixture@example.invalid");
        fclose(fp); cJSON_free(printed); cJSON_Delete(root);
    }
    for (const auto& invalid : {
        "{\"appleEmail\":\"x\",\"applePassword\":\"p\\u0000tail\"}\n",
        "{\"appleEmail\":\"x\",\"applePassword\":\"a\",\"applePassword\":\"b\"}\n",
        "{\"appleEmail\":\"x\",\"applePassword\":\"p\"} trailing\n",
        "{\"appleEmail\":\"x\",\"applePassword\":\"p\",\"storeBinding\":{}}\n",
        "{\"appleEmail\":\"x\",\"applePassword\":\"p\"}"}) {
        FILE* fp = record(invalid);
        std::string user, password;
        assert(!auth_input::read_credentials(fp, user, password));
        assert(user.empty() && password.empty()); fclose(fp);
    }
    FILE* codes = record("{\"code\":\"123456\"}\n{\"code\":\"987654\"}\n{\"code\":\"1234567\"}\n");
    std::string code;
    assert(auth_input::read_code(codes, code) && code == "123456");
    assert(auth_input::read_code(codes, code) && code == "987654");
    assert(!auth_input::read_code(codes, code) && code.empty()); fclose(codes);
    std::istringstream args("--login\nfixture@example.invalid: \tpass:word\\\" \t\r\n");
    std::vector<std::string> parsed;
    assert(auth_input::read_arguments(args, parsed));
    assert(parsed.size() == 2 && parsed[1] == "fixture@example.invalid: \tpass:word\\\" \t\r");
    std::istringstream nul(std::string("a\0b\n", 4));
    assert(!auth_input::read_arguments(nul, parsed));
    puts("auth input: JSON/stdin/arguments preserve special and long credentials; malformed records rejected");
}
