#include "room_selection_store.h"

#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>

enum {
    RoomSelectionVersion = 3,
};

static const char RoomSelectionDirectory[] = "sdmc:/switch/RyuLink";
static const char RoomSelectionPath[] = "sdmc:/switch/RyuLink/room_selection.dat";
static const char RoomSelectionTemporaryPath[] = "sdmc:/switch/RyuLink/room_selection.dat.tmp";

typedef struct {
    char magic[4];
    uint32_t version;
    RyuLinkStoredRoomSelection selection;
    uint32_t crc32;
} StoredRoomSelectionFile;

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

bool ryuLinkRoomSelectionLoad(RyuLinkStoredRoomSelection *selection) {
    FILE *file;

    if (!selection || !(file = fopen(RoomSelectionPath, "rb"))) return false;

    char header[8];
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        ryuLinkRoomSelectionClear();
        return false;
    }
    uint32_t version;
    memcpy(&version, header + 4, sizeof(version));
    bool valid = false;
    if (!memcmp(header, "RYLR", 4) && version == RoomSelectionVersion) {
        StoredRoomSelectionFile stored;
        memcpy(&stored, header, sizeof(header));
        bool complete = fread((char *)&stored + sizeof(header), 1,
                              sizeof(stored) - sizeof(header), file) ==
                        sizeof(stored) - sizeof(header);
        if (complete) complete = fgetc(file) == EOF;
        complete = complete && fclose(file) == 0;
        valid = complete &&
                stored.crc32 == crc32(&stored, offsetof(StoredRoomSelectionFile, crc32)) &&
                stored.selection.active && stored.selection.room_id != 0;
        if (valid) *selection = stored.selection;
    } else {
        fclose(file);
    }

    if (!valid) {
        ryuLinkRoomSelectionClear();
        return false;
    }
    return true;
}

bool ryuLinkRoomSelectionSave(const RyuLinkStoredRoomSelection *selection) {
    StoredRoomSelectionFile stored;
    FILE *file;

    if (!selection || !selection->active || selection->room_id == 0) return false;
    if (mkdir(RoomSelectionDirectory, 0777) != 0 && errno != EEXIST) return false;
    memset(&stored, 0, sizeof(stored));
    memcpy(stored.magic, "RYLR", 4);
    stored.version = RoomSelectionVersion;
    stored.selection = *selection;
    stored.crc32 = crc32(&stored, offsetof(StoredRoomSelectionFile, crc32));
    file = fopen(RoomSelectionTemporaryPath, "wb");
    if (!file) return false;
    bool written = fwrite(&stored, 1, sizeof(stored), file) == sizeof(stored) && fflush(file) == 0;
    bool closed = fclose(file) == 0;
    written = written && closed;
    if (!written) {
        remove(RoomSelectionTemporaryPath);
        return false;
    }
    remove(RoomSelectionPath);
    if (rename(RoomSelectionTemporaryPath, RoomSelectionPath) != 0) {
        remove(RoomSelectionTemporaryPath);
        return false;
    }
    return true;
}

void ryuLinkRoomSelectionClear(void) {
    remove(RoomSelectionTemporaryPath);
    remove(RoomSelectionPath);
}
