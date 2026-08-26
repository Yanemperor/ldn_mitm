#include "evidence_redaction.h"

#include <cstdio>
#include <cstring>

static int failures = 0;

static void expect(const char *input, const char *expected) {
    char output[2048];
    size_t length = 0;
    if (!ryuLinkEvidenceRedactLine(input, output, sizeof(output), &length) ||
        std::strcmp(output, expected) != 0 || length != std::strlen(expected)) {
        std::printf("FAIL input=%s\nexpected=%s\nactual=%s\n", input, expected, output);
        ++failures;
    }
}

int main() {
    expect("peer=10.114.0.42 port=31820\n", "peer=<ip> port=31820\n");
    expect("mac=aa:bb:cc:dd:ee:ff\n", "mac=<mac>\n");
    expect("device=0123456789abcdef0123456789abcdef\n", "device=<id>\n");
    expect("Authorization: Bearer abcdef\n", "<redacted-secret-line>\n");
    expect("ticket=opaque-value\n", "<redacted-secret-line>\n");
    expect("ordinary log line errno=22\n", "ordinary log line errno=22\n");
    expect("bad-ip=999.1.1.1\n", "bad-ip=999.1.1.1\n");
    expect("pc=0x0000007100123456 title=0100152000022000\n",
           "pc=0x0000007100123456 title=<id>\n");
    expect("peer=\xE4\xB8\xAD\xE6\x96\x87\n", "peer=\xE4\xB8\xAD\xE6\x96\x87\n");
    expect("broken=\xC3\x28\n", "broken=?(\n");
    expect("truncated=\xE2\x82", "truncated=??");
    if (failures != 0) return 1;
    std::puts("evidence redaction: 11/11 passed");
    return 0;
}
