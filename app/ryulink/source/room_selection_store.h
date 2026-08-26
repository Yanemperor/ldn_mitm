#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool active;
    uint64_t room_id;
    char room_name[101];
    uint64_t revision;
} RyuLinkStoredRoomSelection;

bool ryuLinkRoomSelectionLoad(RyuLinkStoredRoomSelection *selection);
bool ryuLinkRoomSelectionSave(const RyuLinkStoredRoomSelection *selection);
void ryuLinkRoomSelectionClear(void);
