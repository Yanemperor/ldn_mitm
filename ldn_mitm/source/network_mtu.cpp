#include "network_mtu.hpp"
#include "debug.hpp"
#include "nifm_manager.hpp"

#include <cstring>

namespace ams::mitm::ldn {

    namespace {

        constexpr u16 TargetMtu = 1500;
        const Result ResultMtuVerifyFailed = MAKERESULT(0xCB, 65);

        bool SameUuid(const Uuid &lhs, const Uuid &rhs) {
            return std::memcmp(lhs.uuid, rhs.uuid, sizeof(lhs.uuid)) == 0;
        }

    }

    Result EnsureCurrentNetworkMtu1500() {
        ScopedNifmSession session;
        if (R_FAILED(session.GetResult())) {
            LogFormat("MTU_ERROR stage=initialize rc=%x", session.GetResult());
            return session.GetResult();
        }

        NifmNetworkProfileData profile{};
        Result rc = nifmGetCurrentNetworkProfile(&profile);
        if (R_FAILED(rc)) {
            LogFormat("MTU_ERROR stage=get_profile rc=%x", rc);
            return rc;
        }

        const u16 old_mtu = profile.ip_setting_data.mtu;
        LogFormat("MTU_PROFILE mtu=%u", old_mtu);
        if (old_mtu == TargetMtu) {
            LogFormat("MTU_ALREADY_OK");
            R_SUCCEED();
        }

        profile.ip_setting_data.mtu = TargetMtu;
        Uuid written_uuid = profile.uuid;
        rc = nifmSetNetworkProfile(&profile, &written_uuid);
        if (R_FAILED(rc)) {
            LogFormat("MTU_UPDATE old=%u new=%u result=%x", old_mtu, TargetMtu, rc);
            LogFormat("MTU_ERROR stage=set_profile rc=%x", rc);
            return rc;
        }

        NifmNetworkProfileData verified{};
        rc = nifmGetCurrentNetworkProfile(&verified);
        if (R_FAILED(rc)) {
            LogFormat("MTU_ERROR stage=verify_profile rc=%x", rc);
            return rc;
        }
        if (!SameUuid(profile.uuid, verified.uuid) || verified.ip_setting_data.mtu != TargetMtu) {
            LogFormat("MTU_VERIFY_FAIL expected=%u actual=%u rc=0", TargetMtu, verified.ip_setting_data.mtu);
            return ResultMtuVerifyFailed;
        }

        LogFormat("MTU_UPDATE old=%u new=%u result=OK", old_mtu, TargetMtu);
        R_SUCCEED();
    }

}
