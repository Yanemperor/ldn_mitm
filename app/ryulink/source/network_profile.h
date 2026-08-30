/**
 * @file network_profile.h
 * @brief Safe access to the current Switch wireless network profile.
 */

#pragma once

#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Reads the currently active network profile. */
Result ryuLinkNetworkProfileReadCurrent(NifmNetworkProfileData *out_profile);

/**
 * Sets the active profile to a static IPv4 configuration.
 *
 * Addresses use dotted-decimal host order: 192.168.1.20 is 0xC0A80114.
 * DNS, proxy, and Wi-Fi credentials are preserved.
 */
Result ryuLinkNetworkProfileSetCurrentStaticIpv4(u32 address, u32 subnet_mask,
                                                  u32 gateway);

/** Sets and verifies the MTU of the active profile. */
Result ryuLinkNetworkProfileSetCurrentMtu(u16 mtu);

/** Sets and verifies both the static IPv4 configuration and MTU in one write. */
Result ryuLinkNetworkProfileSetCurrentStaticIpv4AndMtu(u32 address,
                                                        u32 subnet_mask,
                                                        u32 gateway, u16 mtu);

#ifdef __cplusplus
}
#endif
