#include "session_store.h"

#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>

enum { SessionVersion = 1 };

static const char SessionDirectory[] = "sdmc:/switch/RyuLink";
static const char SessionPath[] = "sdmc:/switch/RyuLink/session.dat";
static const char SessionTemporaryPath[] = "sdmc:/switch/RyuLink/session.dat.tmp";

typedef struct {
    char magic[4];
    uint32_t version;
    RyuLinkStoredSession session;
    uint32_t crc32;
} StoredSessionFile;

static uint32_t crc32(const void *data, size_t size) {
    const unsigned char *bytes = data;
    uint32_t value = 0xffffffffU;
    for (size_t i = 0; i < size; ++i) {
        value ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            value = (value >> 1) ^ (0xedb88320U & -(int32_t)(value & 1));
        }
    }
    return ~value;
}

static bool valid_string(const char *value, size_t size) {
    return value[0] && memchr(value, '\0', size) != NULL;
}

bool ryuLinkSessionLoad(RyuLinkStoredSession *session) {
    StoredSessionFile stored;
    FILE *file;

    if (!session || !(file = fopen(SessionPath, "rb"))) return false;
    bool complete = fread(&stored, 1, sizeof(stored), file) == sizeof(stored);
    if (complete) complete = fgetc(file) == EOF;
    complete = complete && fclose(file) == 0;
    bool valid = complete &&
                 !memcmp(stored.magic, "RYLS", 4) && stored.version == SessionVersion &&
                 stored.crc32 == crc32(&stored, offsetof(StoredSessionFile, crc32)) &&
                 valid_string(stored.session.installation_id, sizeof(stored.session.installation_id)) &&
                 valid_string(stored.session.session_token, sizeof(stored.session.session_token));
    if (!valid) {
        ryuLinkSessionClear();
        return false;
    }
    *session = stored.session;
    return true;
}

bool ryuLinkSessionSave(const RyuLinkStoredSession *session) {
    StoredSessionFile stored;
    FILE *file;

    if (!session || !valid_string(session->installation_id, sizeof(session->installation_id)) ||
        !valid_string(session->session_token, sizeof(session->session_token))) return false;
    if (mkdir(SessionDirectory, 0777) != 0 && errno != EEXIST) return false;
    memset(&stored, 0, sizeof(stored));
    memcpy(stored.magic, "RYLS", 4);
    stored.version = SessionVersion;
    stored.session = *session;
    stored.crc32 = crc32(&stored, offsetof(StoredSessionFile, crc32));
    file = fopen(SessionTemporaryPath, "wb");
    if (!file) return false;
    bool written = fwrite(&stored, 1, sizeof(stored), file) == sizeof(stored) &&
                   fflush(file) == 0;
    bool closed = fclose(file) == 0;
    written = written && closed;
    if (!written) {
        remove(SessionTemporaryPath);
        return false;
    }
    remove(SessionPath);
    if (rename(SessionTemporaryPath, SessionPath) != 0) {
        remove(SessionTemporaryPath);
        return false;
    }
    return true;
}

void ryuLinkSessionClear(void) {
    remove(SessionTemporaryPath);
    remove(SessionPath);
}
