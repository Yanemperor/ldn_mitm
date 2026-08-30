#include <cassert>
#include <cstring>

extern "C" {
#include "network_profile.h"
}

static NifmNetworkProfileData profile;
static NifmServiceType service_type;
static int write_count;
static int wireless_enable_count;

extern "C" Result nifmInitialize(NifmServiceType type) {
    service_type = type;
    return 0;
}
extern "C" void nifmExit(void) {}
extern "C" Result nifmGetCurrentNetworkProfile(NifmNetworkProfileData *out_profile) {
    *out_profile = profile;
    return 0;
}
extern "C" Result nifmSetNetworkProfile(const NifmNetworkProfileData *new_profile,
                                         Uuid *uuid) {
    assert(service_type == NifmServiceType_Admin);
    profile = *new_profile;
    *uuid = profile.uuid;
    ++write_count;
    return 0;
}
extern "C" Result nifmSetWirelessCommunicationEnabled(bool enable) {
    assert(enable);
    ++wireless_enable_count;
    return 0;
}

static void expect_address(const NifmIpV4Address &address, u32 expected) {
    assert(address.addr[0] == (u8)(expected >> 24));
    assert(address.addr[1] == (u8)(expected >> 16));
    assert(address.addr[2] == (u8)(expected >> 8));
    assert(address.addr[3] == (u8)expected);
}

int main() {
    std::memset(&profile, 0, sizeof(profile));
    profile.ip_setting_data.ip_address_setting.is_automatic = 1;
    profile.ip_setting_data.mtu = 1400;

    assert(ryuLinkNetworkProfileSetCurrentStaticIpv4AndMtu(
               0xC0A80114u, 0xFFFFFF00u, 0xC0A80101u, 1500) == 0);
    assert(write_count == 1);
    assert(wireless_enable_count == 1);
    assert(profile.ip_setting_data.ip_address_setting.is_automatic == 0);
    expect_address(profile.ip_setting_data.ip_address_setting.current_addr, 0xC0A80114u);
    expect_address(profile.ip_setting_data.ip_address_setting.subnet_mask, 0xFFFFFF00u);
    expect_address(profile.ip_setting_data.ip_address_setting.gateway, 0xC0A80101u);
    assert(profile.ip_setting_data.mtu == 1500);

    assert(ryuLinkNetworkProfileSetCurrentMtu(1500) == 0);
    assert(write_count == 1);
    assert(ryuLinkNetworkProfileSetCurrentMtu(0) != 0);
    assert(write_count == 1);
    assert(ryuLinkNetworkProfileSetCurrentMtu(1450) == 0);
    assert(write_count == 2);
    assert(wireless_enable_count == 2);
    assert(profile.ip_setting_data.mtu == 1450);
}
