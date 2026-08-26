#include "network_mtu_manager.hpp"
#include "debug.hpp"
#include "nifm_manager.hpp"

#include <atomic>
#include <cstring>

namespace ams::mitm::ldn::network_mtu {

    namespace {

        constexpr u16 TargetMtu = 1500;
        constexpr size_t ThreadStackSize = 0x4000;
        constexpr auto RetryInterval = TimeSpan::FromSeconds(1);
        constexpr auto VerifiedInterval = TimeSpan::FromSeconds(5);
        constexpr auto StopJoinInterval = TimeSpan::FromMilliSeconds(10);
        constexpr int StopJoinAttempts = 25;

        class NetworkMtuManager {
            private:
                os::SdkMutex m_mutex;
                os::ThreadType m_thread{};
                alignas(os::ThreadStackAlignment) u8 m_stack[ThreadStackSize]{};
                os::Event m_wakeup{os::EventClearMode_AutoClear};
                std::atomic_bool m_stop{false};
                bool m_running = false;
                Uuid m_last_verified_uuid{};
                bool m_have_verified_uuid = false;
                bool m_waiting_for_profile = false;
                const char *m_last_error_stage = nullptr;
                Result m_last_error = 0;

                static void Worker(void *arg) {
                    static_cast<NetworkMtuManager *>(arg)->WorkerLoop();
                }

                static bool SameUuid(const Uuid &lhs, const Uuid &rhs) {
                    return std::memcmp(lhs.uuid, rhs.uuid, sizeof(lhs.uuid)) == 0;
                }

                void LogError(const char *stage, Result rc) {
                    if (stage != m_last_error_stage || rc.GetValue() != m_last_error.GetValue()) {
                        LogFormat("MTU_ERROR stage=%s rc=%x", stage, rc);
                        m_last_error_stage = stage;
                        m_last_error = rc;
                    }
                }

                void ClearError() {
                    m_last_error_stage = nullptr;
                    m_last_error = 0;
                }

                void LogProfile(const char *event, const NifmNetworkProfileData &profile) {
                    const u8 *uuid = profile.uuid.uuid;
                    LogFormat("%s uuid=%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x mtu=%u",
                              event,
                              uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
                              uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15],
                              profile.ip_setting_data.mtu);
                }

                bool EnsureCurrentProfileMtu() {
                    ScopedNifmSession session;
                    if (R_FAILED(session.GetResult())) {
                        LogError("initialize", session.GetResult());
                        return false;
                    }

                    NifmNetworkProfileData current{};
                    Result rc = nifmGetCurrentNetworkProfile(&current);
                    if (R_FAILED(rc)) {
                        if (!m_waiting_for_profile) {
                            LogFormat("MTU_WAIT_PROFILE");
                            m_waiting_for_profile = true;
                        }
                        LogError("get_profile", rc);
                        return false;
                    }

                    m_waiting_for_profile = false;
                    const bool uuid_changed = !m_have_verified_uuid || !SameUuid(current.uuid, m_last_verified_uuid);
                    if (!uuid_changed && current.ip_setting_data.mtu == TargetMtu) {
                        ClearError();
                        return true;
                    }

                    LogProfile("MTU_PROFILE", current);
                    if (current.ip_setting_data.mtu == TargetMtu) {
                        m_last_verified_uuid = current.uuid;
                        m_have_verified_uuid = true;
                        ClearError();
                        LogFormat("MTU_ALREADY_OK");
                        return true;
                    }

                    NifmNetworkProfileData latest{};
                    rc = nifmGetCurrentNetworkProfile(&latest);
                    if (R_FAILED(rc)) {
                        LogError("reread_profile", rc);
                        return false;
                    }
                    if (!SameUuid(current.uuid, latest.uuid)) {
                        LogFormat("MTU_VERIFY_FAIL expected_uuid_changed rc=0");
                        m_have_verified_uuid = false;
                        return false;
                    }
                    if (latest.ip_setting_data.mtu == TargetMtu) {
                        m_last_verified_uuid = latest.uuid;
                        m_have_verified_uuid = true;
                        ClearError();
                        LogFormat("MTU_ALREADY_OK");
                        return true;
                    }

                    const u16 old_mtu = latest.ip_setting_data.mtu;
                    latest.ip_setting_data.mtu = TargetMtu;
                    Uuid written_uuid = latest.uuid;
                    rc = nifmSetNetworkProfile(&latest, &written_uuid);
                    if (R_FAILED(rc)) {
                        LogFormat("MTU_UPDATE old=%u new=%u result=%x", old_mtu, TargetMtu, rc);
                        LogError("set_profile", rc);
                        return false;
                    }

                    NifmNetworkProfileData verified{};
                    rc = nifmGetCurrentNetworkProfile(&verified);
                    if (R_FAILED(rc)) {
                        LogError("verify_profile", rc);
                        return false;
                    }
                    if (!SameUuid(current.uuid, verified.uuid) || verified.ip_setting_data.mtu != TargetMtu) {
                        LogFormat("MTU_VERIFY_FAIL expected=%u actual=%u rc=0", TargetMtu, verified.ip_setting_data.mtu);
                        m_have_verified_uuid = false;
                        return false;
                    }

                    m_last_verified_uuid = verified.uuid;
                    m_have_verified_uuid = true;
                    ClearError();
                    LogFormat("MTU_UPDATE old=%u new=%u result=OK", old_mtu, TargetMtu);
                    return true;
                }

                void WorkerLoop() {
                    while (!m_stop.load()) {
                        const bool verified = EnsureCurrentProfileMtu();
                        m_wakeup.TimedWait(verified ? VerifiedInterval : RetryInterval);
                    }
                }

            public:
                void Start() {
                    std::scoped_lock lk(m_mutex);
                    if (m_running) {
                        return;
                    }

                    m_stop.store(false);
                    m_have_verified_uuid = false;
                    m_waiting_for_profile = false;
                    ClearError();
                    Result rc = os::CreateThread(&m_thread, Worker, this, m_stack, sizeof(m_stack), 0x15, 2);
                    if (R_FAILED(rc)) {
                        LogError("create_thread", rc);
                        return;
                    }
                    m_running = true;
                    os::SetThreadNamePointer(&m_thread, "ldn_mitm::NetworkMtu");
                    os::StartThread(&m_thread);
                }

                void Stop() {
                    {
                        std::scoped_lock lk(m_mutex);
                        if (!m_running) {
                            return;
                        }
                        m_stop.store(true);
                        m_wakeup.Signal();
                    }

                    for (int i = 0; i < StopJoinAttempts; i++) {
                        if (os::TryWaitThread(&m_thread)) {
                            os::DestroyThread(&m_thread);
                            std::scoped_lock lk(m_mutex);
                            m_running = false;
                            return;
                        }
                        os::SleepThread(StopJoinInterval);
                    }
                    LogFormat("MTU_ERROR stage=stop_join rc=timeout");
                }
        };

        NetworkMtuManager &GetManager() {
            static NetworkMtuManager manager;
            return manager;
        }

    }

    void Start() {
        GetManager().Start();
    }

    void Stop() {
        GetManager().Stop();
    }

}
