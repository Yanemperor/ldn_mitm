#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Parses the server's strict dotted-decimal lease, excluding reserved 10.13.37.1. */
bool ryuLinkParseVirtualIp(const char *text, uint32_t *out_ip);

#ifdef __cplusplus
}
#endif
