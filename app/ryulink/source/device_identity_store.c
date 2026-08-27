#include "device_identity_store.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum { DeviceIdentityVersion = 1 };

static const char DeviceIdentityDirectory[] = "sdmc:/switch/RyuLink";
static const char DeviceIdentityPath[] = "sdmc:/switch/RyuLink/device_identity.dat";
static const char DeviceIdentityTemporaryPath[] = "sdmc:/switch/RyuLink/device_identity.dat.tmp";

typedef struct {
    char magic[4];
    uint32_t version;
    RyuLinkDeviceIdentity identity;
    uint32_t crc32;
} StoredDeviceIdentityFile;

static uint32_t crc32(const void *data, size_t size) {
    const unsigned char *bytes = data;
    uint32_t value = 0xffffffffU;
    for (size_t i = 0; i < size; ++i) {
        value ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xedb88320U & -(int32_t)(value & 1));
    }
    return ~value;
}

static bool valid_string(const char *value, size_t size) {
    return value[0] && memchr(value, '\0', size) != NULL;
}

bool ryuLinkDeviceIdentityLoad(RyuLinkDeviceIdentity *identity) {
    StoredDeviceIdentityFile stored;
    FILE *file;
    bool complete;
    if (!identity || !(file = fopen(DeviceIdentityPath, "rb"))) return false;
    complete = fread(&stored, 1, sizeof(stored), file) == sizeof(stored) && fgetc(file) == EOF;
    complete = complete && fclose(file) == 0;
    if (!complete || memcmp(stored.magic, "RYLD", 4) || stored.version != DeviceIdentityVersion ||
        stored.crc32 != crc32(&stored, offsetof(StoredDeviceIdentityFile, crc32)) ||
        !valid_string(stored.identity.device_bootstrap_id, sizeof(stored.identity.device_bootstrap_id))) return false;
    *identity = stored.identity;
    return true;
}

bool ryuLinkDeviceIdentitySave(const RyuLinkDeviceIdentity *identity) {
    StoredDeviceIdentityFile stored;
    FILE *file;
    bool written;
    if (!identity || !valid_string(identity->device_bootstrap_id, sizeof(identity->device_bootstrap_id)) ||
        (identity->server_device_id[0] && !memchr(identity->server_device_id, '\0', sizeof(identity->server_device_id)))) return false;
    if (mkdir(DeviceIdentityDirectory, 0777) != 0 && errno != EEXIST) return false;
    memset(&stored, 0, sizeof(stored));
    memcpy(stored.magic, "RYLD", 4);
    stored.version = DeviceIdentityVersion;
    stored.identity = *identity;
    stored.crc32 = crc32(&stored, offsetof(StoredDeviceIdentityFile, crc32));
    if (!(file = fopen(DeviceIdentityTemporaryPath, "wb"))) return false;
    written = fwrite(&stored, 1, sizeof(stored), file) == sizeof(stored) && fflush(file) == 0;
    written = fclose(file) == 0 && written;
    if (!written) { remove(DeviceIdentityTemporaryPath); return false; }
    remove(DeviceIdentityPath);
    if (rename(DeviceIdentityTemporaryPath, DeviceIdentityPath) != 0) { remove(DeviceIdentityTemporaryPath); return false; }
    return true;
}
