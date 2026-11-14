#include "../lite/network_config.h"
#include <cassert>

static std::vector<std::string> servers(const std::string& text) {
    std::istringstream input(text);
    return network_config::nameservers(input);
}
int main() {
    assert(set_dns_servers(""));
    assert(servers("nameserver 10.0.2.3\n") == std::vector<std::string>{"10.0.2.3"});
    assert(servers("# nameserver 1.2.3.4\nnameserverBroken 1.2.3.4\nnameserver invalid\n") ==
           (std::vector<std::string>{"8.8.8.8", "8.8.4.4"}));
    assert(servers("  nameserver 127.0.0.53 # stub\nnameserver ::1\n") ==
           (std::vector<std::string>{"127.0.0.53", "::1"}));
    assert(set_dns_servers("1.1.1.1; 2001:4860:4860::8888"));
    assert(servers("nameserver 10.0.2.3") ==
           (std::vector<std::string>{"1.1.1.1", "2001:4860:4860::8888"}));
    for (const auto& bad : {"not-an-address", "1.2.3.4,invalid", ", ; ", "999.0.0.1"})
        assert(!set_dns_servers(bad));
    assert(!set_dns_servers(std::string(513, 'x').c_str()));
    std::string too_many;
    for (int i = 0; i < 9; ++i) too_many += "1.1.1.1,";
    assert(!set_dns_servers(too_many.c_str()));
    assert(set_dns_servers(nullptr));
    std::string many;
    for (int i = 0; i < 9; ++i) many += "nameserver 1.1.1.1\n";
    assert(servers(many).size() == 8);

    assert(setenv("SSL_CERT_FILE", "/legacy/fixture.pem", 1) == 0);
    assert(set_ca_bundle(nullptr));
    assert(std::string(get_ca_bundle()) == "/legacy/fixture.pem");
    assert(set_ca_bundle("/explicit/missing.pem"));
    assert(std::string(get_ca_bundle()) == "/explicit/missing.pem");
    assert(!set_ca_bundle(std::string(4097, 'x').c_str()));
    assert(std::string(get_ca_bundle()) == "/explicit/missing.pem");
    unsetenv("SSL_CERT_FILE"); set_ca_bundle(nullptr);
    puts("network configuration: resolver priority/validation/bounds and explicit CA precedence passed");
}
