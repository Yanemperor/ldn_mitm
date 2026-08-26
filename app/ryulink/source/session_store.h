#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    RyuLinkInstallationIdBytes = 33,
    RyuLinkSessionTokenBytes = 128,
};

typedef struct {
    char installation_id[RyuLinkInstallationIdBytes];
    char session_token[RyuLinkSessionTokenBytes];
} RyuLinkStoredSession;

bool ryuLinkSessionLoad(RyuLinkStoredSession *session);
bool ryuLinkSessionSave(const RyuLinkStoredSession *session);
void ryuLinkSessionClear(void);
