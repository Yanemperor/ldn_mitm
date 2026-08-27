#pragma once

#include <stdbool.h>

enum {
    RyuLinkDeviceBootstrapIdBytes = 33,
    RyuLinkServerDeviceIdBytes = 37,
};

typedef struct {
    char device_bootstrap_id[RyuLinkDeviceBootstrapIdBytes];
    char server_device_id[RyuLinkServerDeviceIdBytes];
} RyuLinkDeviceIdentity;

bool ryuLinkDeviceIdentityLoad(RyuLinkDeviceIdentity *identity);
bool ryuLinkDeviceIdentitySave(const RyuLinkDeviceIdentity *identity);
