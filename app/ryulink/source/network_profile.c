#include "network_profile.h"

#include <string.h>

static Result bad_input(void) {
    return MAKERESULT(Module_Libnx, LibnxError_BadInput);
}

static void set_address(NifmIpV4Address *out_address, u32 address) {
    out_address->addr[0] = (u8)(address >> 24);
    out_address->addr[1] = (u8)(address >> 16);
    out_address->addr[2] = (u8)(address >> 8);
    out_address->addr[3] = (u8)address;
}

static bool same_address(const NifmIpV4Address *address, u32 expected) {
    return address->addr[0] == (u8)(expected >> 24) &&
           address->addr[1] == (u8)(expected >> 16) &&
           address->addr[2] == (u8)(expected >> 8) &&
           address->addr[3] == (u8)expected;
}

Result ryuLinkNetworkProfileReadCurrent(NifmNetworkProfileData *out_profile) {
    if (!out_profile) return bad_input();

    Result rc = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(rc)) return rc;
    rc = nifmGetCurrentNetworkProfile(out_profile);
    nifmExit();
    return rc;
}

static Result write_profile(const NifmNetworkProfileData *profile) {
    Result rc = nifmInitialize(NifmServiceType_Admin);
    if (R_FAILED(rc)) return rc;

    Uuid uuid = profile->uuid;
    rc = nifmSetNetworkProfile(profile, &uuid);
    if (R_SUCCEEDED(rc)) rc = nifmSetWirelessCommunicationEnabled(true);
    nifmExit();
    return rc;
}

static Result write_and_verify(const NifmNetworkProfileData *profile,
                               bool verify_ipv4, u32 address, u32 subnet_mask,
                               u32 gateway, bool verify_mtu, u16 mtu) {
    Result rc = write_profile(profile);
    if (R_FAILED(rc)) return rc;

    NifmNetworkProfileData actual;
    rc = ryuLinkNetworkProfileReadCurrent(&actual);
    if (R_FAILED(rc)) return rc;

    if (verify_ipv4) {
        const NifmIpAddressSetting *ip = &actual.ip_setting_data.ip_address_setting;
        if (ip->is_automatic != 0 || !same_address(&ip->current_addr, address) ||
            !same_address(&ip->subnet_mask, subnet_mask) ||
            !same_address(&ip->gateway, gateway)) return bad_input();
    }
    if (verify_mtu && actual.ip_setting_data.mtu != mtu) return bad_input();
    return 0;
}

Result ryuLinkNetworkProfileSetCurrentStaticIpv4(u32 address, u32 subnet_mask,
                                                  u32 gateway) {
    NifmNetworkProfileData profile;
    Result rc = ryuLinkNetworkProfileReadCurrent(&profile);
    if (R_FAILED(rc)) return rc;

    NifmIpAddressSetting *ip = &profile.ip_setting_data.ip_address_setting;
    ip->is_automatic = 0;
    set_address(&ip->current_addr, address);
    set_address(&ip->subnet_mask, subnet_mask);
    set_address(&ip->gateway, gateway);
    return write_and_verify(&profile, true, address, subnet_mask, gateway, false, 0);
}

Result ryuLinkNetworkProfileSetCurrentMtu(u16 mtu) {
    if (mtu == 0) return bad_input();

    NifmNetworkProfileData profile;
    Result rc = ryuLinkNetworkProfileReadCurrent(&profile);
    if (R_FAILED(rc)) return rc;
    if (profile.ip_setting_data.mtu == mtu) return 0;

    profile.ip_setting_data.mtu = mtu;
    return write_and_verify(&profile, false, 0, 0, 0, true, mtu);
}

Result ryuLinkNetworkProfileSetCurrentStaticIpv4AndMtu(u32 address,
                                                        u32 subnet_mask,
                                                        u32 gateway, u16 mtu) {
    if (mtu == 0) return bad_input();

    NifmNetworkProfileData profile;
    Result rc = ryuLinkNetworkProfileReadCurrent(&profile);
    if (R_FAILED(rc)) return rc;

    NifmIpAddressSetting *ip = &profile.ip_setting_data.ip_address_setting;
    ip->is_automatic = 0;
    set_address(&ip->current_addr, address);
    set_address(&ip->subnet_mask, subnet_mask);
    set_address(&ip->gateway, gateway);
    profile.ip_setting_data.mtu = mtu;
    return write_and_verify(&profile, true, address, subnet_mask, gateway, true, mtu);
}
