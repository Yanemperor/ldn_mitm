#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>

extern "C" {
#include "auth.h"
#include "device_identity_store.h"
#include "ldn_mitm_ipc.h"
#include "session_store.h"
#include "virtual_ip.h"
}

struct CURL { int unused; };
struct curl_mime { int unused; };
struct curl_mimepart { int unused; };

static u32 g_command_id;
static u32 g_input;
static int g_relay_off_calls;
static bool g_start_request_fails;
static long g_http_status;

extern "C" Result svcConnectToNamedPort(Handle *handle, const char *) { *handle = 1; return 0; }
extern "C" Result smGetService(Service *, const char *) { return -1; }
extern "C" void serviceCreate(Service *service, Handle) { service->open = 1; }
extern "C" void serviceClose(Service *service) { service->open = 0; }
extern "C" Result serviceDispatchInMock(Service *, u32 command_id, u32 input) {
    g_command_id = command_id;
    g_input = input;
    if (command_id == 65014 && input == 0) ++g_relay_off_calls;
    return 0;
}
extern "C" Result serviceDispatchOutMock(void) { return 0; }
extern "C" Result serviceDispatchInOutMock(void) { return 0; }
extern "C" Result serviceDispatchMock(Service *, u32) { return 0; }
extern "C" Result socketInitializeDefault(void) { return 0; }
extern "C" void socketExit(void) {}
extern "C" uint64_t armGetSystemTick(void) { return 0; }
extern "C" uint64_t armTicksToNs(uint64_t ticks) { return ticks; }
extern "C" void svcSleepThread(uint64_t) {}
extern "C" void randomGet(void *out, size_t size) { memset(out, 0, size); }

CURLcode curl_global_init(long) { return CURLE_OK; }
void curl_global_cleanup(void) {}
CURL *curl_easy_init(void) { static CURL curl; return &curl; }
void curl_easy_cleanup(CURL *) {}
CURLcode curl_easy_setopt(CURL *, int, ...) { return CURLE_OK; }
CURLcode curl_easy_perform(CURL *) { return g_start_request_fails ? CURLE_FAILED_INIT : CURLE_OK; }
CURLcode curl_easy_getinfo(CURL *, int info, ...) {
    va_list args;
    va_start(args, info);
    *va_arg(args, long *) = g_http_status;
    va_end(args);
    return CURLE_OK;
}
const char *curl_easy_strerror(CURLcode) { return "mock failure"; }
char *curl_easy_escape(CURL *, const char *text, int) { return strdup(text); }
void curl_free(void *value) { free(value); }
curl_slist *curl_slist_append(curl_slist *list, const char *) { return list; }
void curl_slist_free_all(curl_slist *) {}
curl_mime *curl_mime_init(CURL *) { return nullptr; }
curl_mimepart *curl_mime_addpart(curl_mime *) { return nullptr; }
CURLcode curl_mime_name(curl_mimepart *, const char *) { return CURLE_OK; }
CURLcode curl_mime_filename(curl_mimepart *, const char *) { return CURLE_OK; }
CURLcode curl_mime_type(curl_mimepart *, const char *) { return CURLE_OK; }
CURLcode curl_mime_filedata(curl_mimepart *, const char *) { return CURLE_OK; }
void curl_mime_free(curl_mime *) {}
extern "C" bool ryuLinkDeviceIdentityLoad(RyuLinkDeviceIdentity *) { return false; }
extern "C" bool ryuLinkDeviceIdentitySave(const RyuLinkDeviceIdentity *) { return true; }
extern "C" bool ryuLinkSessionLoad(RyuLinkStoredSession *) { return false; }
extern "C" bool ryuLinkSessionSave(const RyuLinkStoredSession *) { return true; }
extern "C" void ryuLinkSessionClear(void) {}
extern "C" const char *ryuLinkLocalize(const char *english, const char *) { return english; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wsometimes-uninitialized"
extern "C" {
#include "../source/virtual_ip.c"
#include "../source/ldn_mitm_ipc.c"
#include "../source/auth.c"
}
#pragma clang diagnostic pop

static void reset_ipc_capture(void) {
    ryuLinkLdnMitmIpcExit();
    g_command_id = 0;
    g_input = 0;
    g_relay_off_calls = 0;
}

static void enable_relay_for_failure_test(void) {
    reset_ipc_capture();
    assert(ryuLinkLdnMitmIpcSetInternetRelayEnabled(true));
    assert(g_command_id == 65014u);
    assert(g_input == 1u);
    g_relay_off_calls = 0;
}

int main() {
    uint32_t ip = 0;
    assert(ryuLinkParseVirtualIp("10.13.0.32", &ip));
    assert(ip == 0x0A0D0020u);
    reset_ipc_capture();
    assert(ryuLinkLdnMitmIpcSetVirtualIp(ip));
    assert(g_command_id == 65015u);
    assert(g_input == 0x0A0D0020u);
    assert(g_input == 168624160u);

    RyuLinkAuthSession session{};
    enable_relay_for_failure_test();
    g_start_request_fails = true;
    g_http_status = 0;
    ryuLinkAuthStart(&session);
    assert(session.state == RyuLinkAuth_Error);
    assert(g_relay_off_calls == 1);
    assert(g_command_id == 65014u);
    assert(g_input == 0u);

    memset(&session, 0, sizeof(session));
    enable_relay_for_failure_test();
    g_start_request_fails = false;
    g_http_status = 401;
    g_has_persisted_session = true;
    strcpy(g_stored_session.session_token, "test-session");
    assert(!ryuLinkAuthRestore(&session));
    assert(session.state == RyuLinkAuth_Error);
    assert(g_relay_off_calls == 1);
    assert(g_command_id == 65014u);
    assert(g_input == 0u);
}
