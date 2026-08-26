/**
 * @file ldn_mitm_ipc.h
 * @brief Thin client for ldn_mitm's public configuration service.
 */

#pragma once

#include <stdbool.h>

enum { RyuLinkLdnMitmRelayNameSize = 32 };

typedef struct {
    bool enabled;
    bool internet_relay_enabled;
    char selected_server[RyuLinkLdnMitmRelayNameSize];
} RyuLinkLdnMitmRelayStatus;

/** Opens the public ldn_mitm configuration service when it is available. */
bool ryuLinkLdnMitmIpcInitialize(void);

/** Closes any handles acquired by ryuLinkLdnMitmIpcInitialize. */
void ryuLinkLdnMitmIpcExit(void);

/**
 * Reads the fixed Relay profile currently selected by ldn_mitm.
 *
 * This function never changes ldn_mitm configuration. It returns false unless
 * the sysmodule and Internet Relay are enabled and a Relay server is selected.
 */
bool ryuLinkLdnMitmIpcGetRelayStatus(RyuLinkLdnMitmRelayStatus *out_status);

/** Enables or disables Internet Relay without changing other ldn_mitm settings. */
bool ryuLinkLdnMitmIpcSetInternetRelayEnabled(bool enabled);
