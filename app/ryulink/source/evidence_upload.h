#pragma once

#include "auth.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool ran;
    bool success;
    uint8_t files_collected;
    uint32_t bundle_bytes;
    char evidence_id[65];
    char message[160];
} RyuLinkEvidenceUpload;

/**
 * Collect bounded diagnostic log tails, redact identifiers and credentials,
 * and upload the resulting text bundle over the authenticated HTTPS session.
 */
bool ryuLinkEvidenceUploadRun(RyuLinkAuthSession *session,
                              RyuLinkEvidenceUpload *upload);
