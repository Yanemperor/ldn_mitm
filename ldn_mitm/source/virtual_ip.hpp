#pragma once

#include <cstdint>

namespace ams::mitm::ldn::relay {

    constexpr std::uint32_t VirtualIpPoolFirst = 0x0A0D0020u;  /* 10.13.0.32 */
    constexpr std::uint32_t VirtualIpPoolLast  = 0x0A0DFFFEu;  /* 10.13.255.254 */

    constexpr bool IsServerVirtualIp(std::uint32_t ip) {
        return ip >= VirtualIpPoolFirst && ip <= VirtualIpPoolLast;
    }

}
