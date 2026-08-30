/**
 * @file ldn_mitm_ipc.c
 * @brief Thin IPC client for ldn_mitm.
 */

#include "ldn_mitm_ipc.h"

#include <string.h>
#include <switch.h>

/* Public command IDs from ldn_mitm/source/interfaces/iconfig.hpp.
 * That header is a C++ stratosphere interface, so the C NRO sends this small
 * ABI directly rather than linking or copying any ldn_mitm implementation. */
enum {
    LdnMitmCmdCreateConfigService = 65000,
    LdnMitmCmdGetEnabled = 65004,
    LdnMitmCmdGetInternetRelay = 65008,
    LdnMitmCmdGetRelayServerCount = 65010,
    LdnMitmCmdGetRelayServerName = 65011,
    LdnMitmCmdGetSelectedRelayServer = 65012,
    LdnMitmCmdSetInternetRelayEnabled = 65014,
    LdnMitmCmdSetVirtualIp = 65015,
    LdnMitmCmdSetRelayCredential = 65017,
};

static Service g_ldn_service;
static Service g_config_service;
static bool g_ldn_service_open;
static bool g_config_service_open;

bool ryuLinkLdnMitmIpcInitialize(void) {
    Handle handle;
    Result rc;

    if (g_config_service_open) return true;

    rc = svcConnectToNamedPort(&handle, "ldnmitm");
    if (R_SUCCEEDED(rc)) {
        serviceCreate(&g_config_service, handle);
        g_config_service_open = true;
        return true;
    }

    rc = smGetService(&g_ldn_service, "ldn:u");
    if (R_FAILED(rc)) return false;
    g_ldn_service_open = true;

    rc = serviceDispatch(&g_ldn_service, LdnMitmCmdCreateConfigService,
                         .out_num_objects = 1,
                         .out_objects = &g_config_service);
    if (R_SUCCEEDED(rc)) {
        g_config_service_open = true;
        return true;
    }

    serviceClose(&g_ldn_service);
    g_ldn_service_open = false;
    return false;
}

void ryuLinkLdnMitmIpcExit(void) {
    if (g_config_service_open) {
        serviceClose(&g_config_service);
        g_config_service_open = false;
    }
    if (g_ldn_service_open) {
        serviceClose(&g_ldn_service);
        g_ldn_service_open = false;
    }
}

bool ryuLinkLdnMitmIpcGetRelayStatus(RyuLinkLdnMitmRelayStatus *out_status) {
    u32 enabled;
    u32 internet_relay_enabled;
    u32 server_count;
    u32 selected_server;
    char selected_server_name[RyuLinkLdnMitmRelayNameSize];
    Result rc;

    if (!out_status) return false;
    memset(out_status, 0, sizeof(*out_status));
    if (!ryuLinkLdnMitmIpcInitialize()) return false;

    rc = serviceDispatchOut(&g_config_service, LdnMitmCmdGetEnabled, enabled);
    if (R_FAILED(rc)) return false;
    rc = serviceDispatchOut(&g_config_service, LdnMitmCmdGetInternetRelay, internet_relay_enabled);
    if (R_FAILED(rc)) return false;
    rc = serviceDispatchOut(&g_config_service, LdnMitmCmdGetRelayServerCount, server_count);
    if (R_FAILED(rc) || server_count == 0) return false;
    rc = serviceDispatchOut(&g_config_service, LdnMitmCmdGetSelectedRelayServer, selected_server);
    if (R_FAILED(rc) || selected_server >= server_count) return false;
    rc = serviceDispatchInOut(&g_config_service, LdnMitmCmdGetRelayServerName,
                              selected_server, selected_server_name);
    if (R_FAILED(rc)) return false;

    out_status->enabled = enabled != 0;
    out_status->internet_relay_enabled = internet_relay_enabled != 0;
    memcpy(out_status->selected_server, selected_server_name, sizeof(selected_server_name));
    out_status->selected_server[sizeof(out_status->selected_server) - 1] = '\0';
    return out_status->enabled && out_status->internet_relay_enabled && out_status->selected_server[0];
}

bool ryuLinkLdnMitmIpcSetInternetRelayEnabled(bool enabled) {
    if (!ryuLinkLdnMitmIpcInitialize()) return false;
    return R_SUCCEEDED(serviceDispatchIn(&g_config_service,
                                         LdnMitmCmdSetInternetRelayEnabled, enabled));
}

bool ryuLinkLdnMitmIpcSetVirtualIp(uint32_t ip) {
    if (!ryuLinkLdnMitmIpcInitialize()) return false;
    return R_SUCCEEDED(serviceDispatchIn(&g_config_service, LdnMitmCmdSetVirtualIp, ip));
}

bool ryuLinkLdnMitmIpcSetRelayCredential(const char *credential) {
    static const char EmptyCredential[] = "";
    const char *value = credential ? credential : EmptyCredential;
    size_t size = strlen(value);
    if (size >= RyuLinkLdnMitmRelayCredentialBytes || !ryuLinkLdnMitmIpcInitialize()) return false;
    return R_SUCCEEDED(serviceDispatch(&g_config_service, LdnMitmCmdSetRelayCredential,
                                       .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
                                       .buffers = { { value, size } }));
}
