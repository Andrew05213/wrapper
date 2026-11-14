#pragma once

#include <arpa/inet.h>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Configure before starting authentication or service threads, like device info.
namespace network_config {
struct Settings { std::string dns, ca_bundle; };
inline Settings& settings() { static Settings value; return value; }
inline bool address(const std::string& text) {
    unsigned char bytes[16];
    return inet_pton(AF_INET, text.c_str(), bytes) == 1 ||
           inet_pton(AF_INET6, text.c_str(), bytes) == 1;
}
inline std::vector<std::string> split(const std::string& list) {
    std::string normalized = list;
    for (char& c : normalized) if (c == ',' || c == ';') c = ' ';
    std::istringstream input(normalized);
    std::vector<std::string> result;
    std::string item;
    while (input >> item) result.push_back(item);
    return result;
}
inline std::vector<std::string> nameservers(std::istream& resolv) {
    auto result = split(settings().dns);
    if (!result.empty()) return result;
    std::string line;
    while (result.size() < 8 && std::getline(resolv, line)) {
        std::istringstream input(line);
        std::string name, value;
        if (input >> name >> value && name == "nameserver" && address(value)) result.push_back(value);
    }
    if (result.empty()) result = {"8.8.8.8", "8.8.4.4"};
    return result;
}
} // namespace network_config

inline bool set_dns_servers(const char* list) {
    const std::string value = list ? list : "";
    if (value.size() > 512) return false;
    const auto servers = network_config::split(value);
    if ((!value.empty() && servers.empty()) || servers.size() > 8) return false;
    for (const auto& server : servers) if (!network_config::address(server)) return false;
    network_config::settings().dns = value;
    return true;
}
inline bool set_ca_bundle(const char* path) {
    const std::string value = path ? path : "";
    if (value.size() > 4096) return false;
    network_config::settings().ca_bundle = value;
    return true;
}
inline const char* get_ca_bundle() {
    const auto& value = network_config::settings().ca_bundle;
    if (!value.empty()) return value.c_str();
    const char* legacy = std::getenv("SSL_CERT_FILE");
    if (legacy && *legacy) return legacy;
    const char* bundled = "/system/etc/security/cacert.pem";
    if (std::ifstream(bundled).good()) return bundled;
    if (std::ifstream("/usr/local/android/cert.pem").good()) return "/usr/local/android/cert.pem";
    return bundled;
}
