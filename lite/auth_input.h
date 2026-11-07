#pragma once

#include "cJSON.h"
#include <cstdio>
#include <cstring>
#include <istream>
#include <string>
#include <vector>

namespace auth_input {
inline void wipe(std::string& s) {
    volatile char* p = s.empty() ? nullptr : &s[0];
    for (size_t i = 0; i < s.size(); ++i) p[i] = 0;
    s.clear();
}

// JSON Lines transport: decode once, preserve all bytes of the decoded string.
// cJSON exposes C strings, so reject escaped NUL rather than silently truncate.
inline bool has_escaped_nul(const std::string& json) {
    for (size_t i = 0; i < json.size(); ++i) {
        if (json[i] == '\\' && ++i < json.size() && json[i] == 'u' &&
            json.compare(i + 1, 4, "0000") == 0) return true;
    }
    return false;
}
inline bool read_record(FILE* input, std::string& line, size_t limit) {
    wipe(line);
    int ch;
    while ((ch = fgetc(input)) != EOF) {
        if (ch == '\n') return !line.empty();
        if (ch == 0 || line.size() >= limit) { wipe(line); return false; }
        line.push_back(static_cast<char>(ch));
    }
    wipe(line);
    return false; // A truncated record must not become a login attempt.
}
inline cJSON* parse_record(const std::string& line) {
    if (line.find('\0') != std::string::npos || has_escaped_nul(line)) return nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(line.c_str(), line.size() + 1, nullptr, true);
    if (root && !cJSON_IsObject(root)) { cJSON_Delete(root); root = nullptr; }
    return root;
}
inline const char* unique_string(cJSON* root, const char* name) {
    const char* value = nullptr;
    for (cJSON* child = root ? root->child : nullptr; child; child = child->next) {
        if (child->string && strcmp(child->string, name) == 0) {
            if (value || !cJSON_IsString(child)) return nullptr;
            value = child->valuestring;
        }
    }
    return value;
}
inline void wipe_json_strings(cJSON* root) {
    for (cJSON* item = root; item; item = item->next) {
        if (cJSON_IsString(item) && item->valuestring) {
            volatile char* p = item->valuestring;
            for (size_t i = 0, n = strlen(item->valuestring); i < n; ++i) p[i] = 0;
        }
        if (item->child) wipe_json_strings(item->child);
    }
}
inline bool read_credentials(FILE* input, std::string& user, std::string& password) {
    wipe(user); wipe(password);
    std::string line;
    if (!read_record(input, line, 32768)) return false;
    cJSON* root = parse_record(line);
    wipe(line);
    const char* u = unique_string(root, "appleEmail");
    const char* p = unique_string(root, "applePassword");
    bool supported = true;
    for (cJSON* item = root ? root->child : nullptr; item; item = item->next)
        if (!item->string || (strcmp(item->string, "appleEmail") && strcmp(item->string, "applePassword")))
            supported = false; // Do not silently drop a downstream storeBinding.
    const bool valid = supported && u && *u && p && *p && strlen(u) <= 512 && strlen(p) <= 4096;
    if (valid) { user = u; password = p; }
    wipe_json_strings(root); cJSON_Delete(root);
    return valid;
}
inline bool read_code(FILE* input, std::string& code) {
    wipe(code);
    std::string line;
    if (!read_record(input, line, 128)) return false;
    cJSON* root = parse_record(line);
    wipe(line);
    const char* p = unique_string(root, "code");
    const bool valid = p && strlen(p) == 6 && strspn(p, "0123456789") == 6;
    if (valid) code = p;
    wipe_json_strings(root); cJSON_Delete(root);
    return valid;
}

// LITE_ARGS_FILE is one argument per LF-delimited line. Whitespace belongs to
// the argument (especially passwords); NUL is an error, never silently removed.
inline bool read_arguments(std::istream& input, std::vector<std::string>& arguments) {
    std::string line;
    while (std::getline(input, line)) {
        if (line.find('\0') != std::string::npos) return false;
        if (!line.empty()) arguments.push_back(line);
    }
    return !input.bad();
}
} // namespace auth_input
