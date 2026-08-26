#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Redact one UTF-8/log line into a caller-owned buffer. */
bool ryuLinkEvidenceRedactLine(const char *input, char *output,
                               size_t output_size, size_t *output_length);

#ifdef __cplusplus
}
#endif
