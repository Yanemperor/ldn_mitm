#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Parses the server's strict dotted-decimal 10.13.0.32–10.13.255.254 lease. */
bool ryuLinkParseVirtualIp(const char *text, uint32_t *out_ip);

#ifdef __cplusplus
}
#endif
