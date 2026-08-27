#include "virtual_ip.h"

#include <ctype.h>

bool ryuLinkParseVirtualIp(const char *text, uint32_t *out_ip) {
    uint32_t octets[4] = {0};
    const char *p = text;
    if (!text || !out_ip) return false;
    for (int index = 0; index < 4; ++index) {
        uint32_t value = 0;
        int digits = 0;
        if (*p == '0' && isdigit((unsigned char)p[1])) return false;
        while (isdigit((unsigned char)*p)) {
            if (++digits > 3) return false;
            value = value * 10 + (uint32_t)(*p++ - '0');
            if (value > 255) return false;
        }
        if (digits == 0 || (index < 3 && *p++ != '.') || (index == 3 && *p != '\0')) return false;
        octets[index] = value;
    }
    *out_ip = (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
    return *out_ip >= 0x0A0D0020u && *out_ip <= 0x0A0DFFFEu;
}
