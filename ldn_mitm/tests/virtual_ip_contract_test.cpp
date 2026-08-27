#include <cassert>
#include "../source/virtual_ip.hpp"

using ams::mitm::ldn::relay::IsServerVirtualIp;

int main() {
    assert(IsServerVirtualIp(0x0A0D0020u));  // 10.13.0.32
    assert(IsServerVirtualIp(0x0A0D2501u));  // 10.13.37.1
    assert(IsServerVirtualIp(0x0A0D25FFu));  // 10.13.37.255
    assert(IsServerVirtualIp(0x0A0DFFFEu));  // 10.13.255.254

    assert(!IsServerVirtualIp(0x0A0D0000u)); // 10.13.0.0
    assert(!IsServerVirtualIp(0x0A0D0001u)); // 10.13.0.1
    assert(!IsServerVirtualIp(0x0A0D001Fu)); // 10.13.0.31
    assert(!IsServerVirtualIp(0x0A0DFFFFu)); // 10.13.255.255
    assert(!IsServerVirtualIp(0x0A0C0101u)); // 10.12.1.1
    assert(!IsServerVirtualIp(0x0A0E0101u)); // 10.14.1.1
    assert(!IsServerVirtualIp(0xC0A80101u)); // 192.168.1.1
}
