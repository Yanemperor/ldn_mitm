#include <cassert>

extern "C" {
#include "virtual_ip.h"
}

static void accept(const char *text, uint32_t expected) {
    uint32_t ip = 0;
    assert(ryuLinkParseVirtualIp(text, &ip));
    assert(ip == expected);
}

static void reject(const char *text) {
    uint32_t ip = 0;
    assert(!ryuLinkParseVirtualIp(text, &ip));
}

int main() {
    accept("10.13.0.32", 0x0A0D0020u);
    accept("10.13.37.1", 0x0A0D2501u);
    accept("10.13.37.255", 0x0A0D25FFu);
    accept("10.13.255.254", 0x0A0DFFFEu);
    reject("10.13.0.0"); reject("10.13.0.1"); reject("10.13.0.31"); reject("10.13.255.255");
    reject("10.12.1.1"); reject("10.14.1.1"); reject("192.168.1.1"); reject("10.13.00.32");
}
