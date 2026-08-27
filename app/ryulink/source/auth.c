#include "auth.h"
#include "app_version.h"
#include "device_identity_store.h"
#include "ldn_mitm_ipc.h"
#include "localization.h"
#include "session_store.h"

#include <curl/curl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define L(english, chinese) ryuLinkLocalize((english), (chinese))

enum { MaxBody = 8192, MaxToken = 4096, TimeoutSeconds = 5 };
static const char Discovery[] = "https://auth.ryulink.xyz/oidc/.well-known/openid-configuration";
static const char Issuer[] = "https://auth.ryulink.xyz/oidc";
static const char AuthHost[] = "auth.ryulink.xyz";
static const char VerificationUri[] = "https://auth.ryulink.xyz/device";
static const char PlayerUrl[] = "https://api.ryulink.xyz/app-api/ryulink/player/me";
static const char PlayerSessionExchangeUrl[] = "https://api.ryulink.xyz/app-api/ryulink/session/exchange";
static const char PlayerSessionLogoutUrl[] = "https://api.ryulink.xyz/app-api/ryulink/session/logout";
static const char DiagnosticEvidenceUrl[] =
    "https://api.ryulink.xyz/app-api/ryulink/diagnostics/evidence";
static const char ClientId[] = "oqjslw0gq1s83h65uyp7m";
static const char ResourceFormValue[] = "https%3A%2F%2Fapi.ryulink.xyz%2Fryulink";
/* Emergency bootstrap only: TLS still verifies the URL host, never this IP. */
static const char AuthDnsFallback[] = "auth.ryulink.xyz:443:167.254.243.6";
static const char ApiDnsFallback[] = "api.ryulink.xyz:443:167.254.243.6";

typedef struct { char data[MaxBody + 1]; size_t length; bool too_large; } Response;
static char g_device_endpoint[512];
static char g_token_endpoint[512];
static char g_device_code[MaxToken + 1];
static char g_access_token[MaxToken + 1];
static CURLcode g_last_curl_result = CURLE_OK;
static RyuLinkStoredSession g_stored_session;
static bool g_has_persisted_session;
static int g_loading_count;

static uint64_t now_ms(void) { return armTicksToNs(armGetSystemTick()) / 1000000ULL; }
static const char *next_object(const char *cursor, char *object, size_t object_size);

static size_t receive(void *data, size_t size, size_t count, void *user) {
    Response *response = (Response *)user;
    size_t bytes = size * count;
    if (bytes > MaxBody - response->length) { response->too_large = true; return 0; }
    memcpy(response->data + response->length, data, bytes);
    response->length += bytes;
    response->data[response->length] = 0;
    return bytes;
}

static bool auth_url(const char *url) {
    size_t host_len = strlen(AuthHost);
    return strncmp(url, "https://", 8) == 0 &&
           strncmp(url + 8, AuthHost, host_len) == 0 &&
           (url[8 + host_len] == '/' || url[8 + host_len] == 0);
}

/* OAuth values are ASCII. Reject unsupported JSON escapes rather than guessing. */
static bool get_string(const char *json, const char *key, char *out, size_t out_size) {
    char member[80]; const char *p; size_t length = 0;
    if (snprintf(member, sizeof(member), "\"%s\"", key) >= (int)sizeof(member)) return false;
    p = strstr(json, member);
    if (!p || !(p = strchr(p + strlen(member), ':'))) return false;
    for (++p; isspace((unsigned char)*p); ++p) {}
    if (*p++ != '\"') return false;
    while (*p && *p != '\"') {
        unsigned char c = (unsigned char)*p++;
        if (c < 0x20 || c == '\\' || length + 1 >= out_size) return false;
        out[length++] = (char)c;
    }
    if (*p++ != '\"') return false;
    out[length] = 0;
    return true;
}

static bool get_u32(const char *json, const char *key, uint32_t *out) {
    char member[80]; const char *p; unsigned long value = 0;
    if (snprintf(member, sizeof(member), "\"%s\"", key) >= (int)sizeof(member)) return false;
    p = strstr(json, member);
    if (!p || !(p = strchr(p + strlen(member), ':'))) return false;
    for (++p; isspace((unsigned char)*p); ++p) {}
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) { value = value * 10 + (unsigned long)(*p++ - '0'); if (value > UINT32_MAX) return false; }
    *out = (uint32_t)value;
    return true;
}

static bool get_bool(const char *json, const char *key, bool *out) {
    char member[80]; const char *p;
    if (snprintf(member, sizeof(member), "\"%s\"", key) >= (int)sizeof(member)) return false;
    p = strstr(json, member);
    if (!p || !(p = strchr(p + strlen(member), ':'))) return false;
    for (++p; isspace((unsigned char)*p); ++p) {}
    if (strncmp(p, "true", 4) == 0) { *out = true; return true; }
    if (strncmp(p, "false", 5) == 0) { *out = false; return true; }
    return false;
}

static bool get_u64(const char *json, const char *key, uint64_t *out) {
    char member[80]; const char *p; uint64_t value = 0;
    if (snprintf(member, sizeof(member), "\"%s\"", key) >= (int)sizeof(member)) return false;
    p = strstr(json, member);
    if (!p || !(p = strchr(p + strlen(member), ':'))) return false;
    for (++p; isspace((unsigned char)*p); ++p) {}
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) {
        uint64_t digit = (uint64_t)(*p++ - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

static bool evidence_id_valid(const char *value) {
    if (value == NULL || strlen(value) != 32) return false;
    for (size_t index = 0; index < 32; ++index) {
        if (!isxdigit((unsigned char)value[index])) return false;
    }
    return true;
}

static bool request(const char *url, const char *form, const char *token, long *status, Response *response) {
    char authorization[MaxToken + 32];
    const char *dns_fallback = strstr(url, "https://auth.ryulink.xyz/") == url ? AuthDnsFallback :
                               strstr(url, "https://api.ryulink.xyz/") == url ? ApiDnsFallback : NULL;
    bool ok = false;

    ryuLinkLoadingBegin();
    for (int attempt = 0; attempt < 2; ++attempt) {
        CURL *curl = curl_easy_init();
        CURLcode result;
        struct curl_slist *headers = NULL;
        struct curl_slist *resolve = NULL;
        if (!curl) {
            g_last_curl_result = CURLE_FAILED_INIT;
            break;
        }
        memset(response, 0, sizeof(*response)); *status = 0;
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, TimeoutSeconds);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, TimeoutSeconds);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, RYULINK_APP_USER_AGENT);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
        if (form) { headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded"); curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form); }
        if (token) { if (snprintf(authorization, sizeof(authorization), "Authorization: Bearer %s", token) >= (int)sizeof(authorization)) { curl_easy_cleanup(curl); break; } headers = curl_slist_append(headers, authorization); }
        if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        if (attempt == 1 && dns_fallback) {
            resolve = curl_slist_append(NULL, dns_fallback);
            curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
        }
        result = curl_easy_perform(curl);
        g_last_curl_result = result;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status);
        curl_slist_free_all(resolve); curl_slist_free_all(headers); curl_easy_cleanup(curl);
        if (result != CURLE_COULDNT_RESOLVE_HOST || !dns_fallback || attempt == 1) {
            ok = result == CURLE_OK && !response->too_large;
            break;
        }
    }
    ryuLinkLoadingEnd();
    return ok;
}

static bool app_request(const char *url, const char *method, const char *body, long *status, Response *response) {
    CURL *curl; struct curl_slist *headers = NULL; char authorization[MaxToken + 32];
    bool ok = false;
    if (!g_access_token[0]) return false;
    ryuLinkLoadingBegin();
    curl = curl_easy_init();
    if (!curl) { ryuLinkLoadingEnd(); return false; }
    memset(response, 0, sizeof(*response)); *status = 0;
    if (snprintf(authorization, sizeof(authorization), "Authorization: Bearer %s", g_access_token) >= (int)sizeof(authorization)) {
        curl_easy_cleanup(curl); ryuLinkLoadingEnd(); return false;
    }
    headers = curl_slist_append(headers, authorization);
    if (body) headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, TimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, TimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, RYULINK_APP_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    if (body) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    g_last_curl_result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status);
    curl_slist_free_all(headers); curl_easy_cleanup(curl);
    ok = g_last_curl_result == CURLE_OK && !response->too_large;
    ryuLinkLoadingEnd();
    return ok;
}

static void fail(RyuLinkAuthSession *session, const char *message) {
    if (g_last_curl_result == CURLE_OPERATION_TIMEDOUT) message = L("Request timed out. Please try again.", "请求超时，请重试。");
    memset(g_device_code, 0, sizeof(g_device_code));
    memset(g_access_token, 0, sizeof(g_access_token));
    (void)ryuLinkLdnMitmIpcSetInternetRelayEnabled(false);
    session->state = RyuLinkAuth_Error;
    snprintf(session->message, sizeof(session->message), "%s", message);
}

static void clear_persisted_session(void) {
    memset(&g_stored_session, 0, sizeof(g_stored_session));
    g_has_persisted_session = false;
    ryuLinkSessionClear();
}

static void revoke_persisted_session(void) {
    Response response;
    long status;
    const char *token = g_access_token[0] ? g_access_token : g_stored_session.session_token;
    if (g_has_persisted_session && token[0]) {
        request(PlayerSessionLogoutUrl, "{}", token, &status, &response);
    }
    clear_persisted_session();
}

static void create_installation_id(char installation_id[RyuLinkInstallationIdBytes]) {
    static const char Hex[] = "0123456789abcdef";
    u8 random[16];
    randomGet(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); ++i) {
        installation_id[i * 2] = Hex[random[i] >> 4];
        installation_id[i * 2 + 1] = Hex[random[i] & 0x0f];
    }
    installation_id[sizeof(random) * 2] = '\0';
}

static void schedule_network_retry(RyuLinkAuthSession *session) {
    uint32_t minimum = session->poll_interval_seconds > 5 ? session->poll_interval_seconds : 5;
    if (session->network_retry_seconds < minimum) session->network_retry_seconds = minimum;
    else if (session->network_retry_seconds < 30) {
        session->network_retry_seconds *= 2;
        if (session->network_retry_seconds > 30) session->network_retry_seconds = 30;
    }
    session->next_poll_ms = now_ms() + (uint64_t)session->network_retry_seconds * 1000ULL;
}

static bool player_me(RyuLinkAuthSession *session) {
    Response response; long status; uint32_t player_status; bool test_access;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (request(PlayerUrl, NULL, g_access_token, &status, &response) && status == 200 &&
            get_string(response.data, "displayName", session->display_name, sizeof(session->display_name)) &&
            get_u32(response.data, "status", &player_status) && player_status <= 1 &&
            get_bool(response.data, "testAccessEnabled", &test_access)) {
            session->player_code[0] = '\0';
            get_string(response.data, "playerCode", session->player_code, sizeof(session->player_code));
            session->account_disabled = player_status == 1;
            session->test_access_enabled = test_access;
            session->vip_active = get_bool(response.data, "vipActive", &session->vip_active) && session->vip_active;
            session->private_room_name[0] = '\0';
            /* Prefer myPrivateRooms[0].name; fall back to legacy myVipRoom.name. */
            {
                const char *rooms = strstr(response.data, "\"myPrivateRooms\"");
                const char *legacy = strstr(response.data, "\"myVipRoom\"");
                char object[512];
                if (rooms && (rooms = strchr(rooms, '[')) && next_object(rooms + 1, object, sizeof(object))) {
                    get_string(object, "name", session->private_room_name, sizeof(session->private_room_name));
                } else if (legacy && next_object(legacy, object, sizeof(object))) {
                    get_string(object, "name", session->private_room_name, sizeof(session->private_room_name));
                }
            }
            session->state = RyuLinkAuth_Authenticated;
            return true;
        }
        if (status == 401) {
            if (g_has_persisted_session) {
                clear_persisted_session();
                fail(session, L("Signed in on another device. Start login again.", "账号已在其他设备登录，请重新登录。"));
            } else {
                fail(session, L("Session expired. Start login again.", "会话已过期，请重新登录。"));
            }
            return false;
        }
        if (status == 403) { fail(session, L("Account has no player access.", "账号没有玩家权限。")); return false; }
        if (status >= 400 && status < 500) { fail(session, L("Player profile was rejected.", "玩家资料被拒绝。")); return false; }
        if (attempt < 2) svcSleepThread((attempt + 1) * 1000000000ULL);
    }
    fail(session, L("Control plane is temporarily unavailable.", "控制平台暂时不可用。")); return false;
}

bool ryuLinkAuthInitialize(void) {
    if (R_FAILED(socketInitializeDefault())) return false;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) { socketExit(); return false; }
    g_has_persisted_session = ryuLinkSessionLoad(&g_stored_session);
    return true;
}

void ryuLinkAuthExit(void) { memset(g_device_code, 0, sizeof(g_device_code)); memset(g_access_token, 0, sizeof(g_access_token)); memset(&g_stored_session, 0, sizeof(g_stored_session)); curl_global_cleanup(); socketExit(); }

bool ryuLinkAuthHasPersistedSession(void) { return g_has_persisted_session; }

void ryuLinkLoadingBegin(void) { ++g_loading_count; }
void ryuLinkLoadingEnd(void) { if (g_loading_count > 0) --g_loading_count; }
bool ryuLinkLoadingActive(void) { return g_loading_count > 0; }

void ryuLinkAuthCancel(RyuLinkAuthSession *session) { revoke_persisted_session(); memset(g_device_code, 0, sizeof(g_device_code)); memset(g_access_token, 0, sizeof(g_access_token)); memset(session, 0, sizeof(*session)); session->state = RyuLinkAuth_Idle; }

bool ryuLinkAuthRestore(RyuLinkAuthSession *session) {
    if (!session || !g_has_persisted_session) return false;
    snprintf(g_access_token, sizeof(g_access_token), "%s", g_stored_session.session_token);
    if (!player_me(session)) return false;
    return true;
}

static bool persist_session(RyuLinkAuthSession *session) {
    Response response;
    long status;
    char token[RyuLinkSessionTokenBytes];

    if (!request(PlayerSessionExchangeUrl, "{}", g_access_token, &status, &response) || status != 200 ||
        !get_string(response.data, "token", token, sizeof(token))) {
        fail(session, L("Unable to save the sign-in session.", "无法保存登录状态。"));
        return false;
    }
    if (!g_stored_session.installation_id[0]) create_installation_id(g_stored_session.installation_id);
    snprintf(g_stored_session.session_token, sizeof(g_stored_session.session_token), "%s", token);
    if (!ryuLinkSessionSave(&g_stored_session)) {
        memset(token, 0, sizeof(token));
        fail(session, L("Unable to save the sign-in session.", "无法保存登录状态。"));
        return false;
    }
    snprintf(g_access_token, sizeof(g_access_token), "%s", token);
    memset(token, 0, sizeof(token));
    g_has_persisted_session = true;
    return true;
}

void ryuLinkAuthStart(RyuLinkAuthSession *session) {
    Response response; long status; char issuer[256]; char verification_uri[512]; char verification_uri_complete[4097]; uint32_t expires; uint32_t interval = 5;
    char form[512]; char message[128]; char *scope; char *resource; char *escaped_user_code;
    ryuLinkAuthCancel(session);
    if (!request(Discovery, NULL, NULL, &status, &response)) {
        snprintf(message, sizeof(message), "Login discovery failed: %s", curl_easy_strerror(g_last_curl_result));
        fail(session, message);
        return;
    }
    if (status != 200) {
        snprintf(message, sizeof(message), "Login discovery HTTP %ld", status);
        fail(session, message);
        return;
    }
    if (!get_string(response.data, "issuer", issuer, sizeof(issuer)) || strcmp(issuer, Issuer) ||
        !get_string(response.data, "device_authorization_endpoint", g_device_endpoint, sizeof(g_device_endpoint)) ||
        !get_string(response.data, "token_endpoint", g_token_endpoint, sizeof(g_token_endpoint)) ||
        !auth_url(g_device_endpoint) || !auth_url(g_token_endpoint)) { fail(session, L("Login discovery format is invalid.", "登录发现信息格式无效。")); return; }
    scope = curl_easy_escape(NULL, "openid ryulink:player", 0);
    resource = curl_easy_escape(NULL, "https://api.ryulink.xyz/ryulink", 0);
    if (!scope || !resource) { curl_free(scope); curl_free(resource); fail(session, L("Unable to start login.", "无法开始登录。")); return; }
    snprintf(form, sizeof(form), "client_id=%s&scope=%s&resource=%s", ClientId, scope, resource);
    curl_free(scope); curl_free(resource);
    if (!request(g_device_endpoint, form, NULL, &status, &response) || status != 200 ||
        !get_string(response.data, "device_code", g_device_code, sizeof(g_device_code)) ||
        !get_string(response.data, "user_code", session->user_code, sizeof(session->user_code)) ||
        !get_string(response.data, "verification_uri", verification_uri, sizeof(verification_uri)) ||
        strcmp(verification_uri, VerificationUri) || !get_u32(response.data, "expires_in", &expires) || !expires) { fail(session, L("Unable to obtain a device code.", "无法获取设备代码。")); return; }
    snprintf(session->verification_uri, sizeof(session->verification_uri), "%s", VerificationUri);
    if (strstr(response.data, "\"verification_uri_complete\"")) {
        if (!get_string(response.data, "verification_uri_complete", verification_uri_complete, sizeof(verification_uri_complete)) ||
            !auth_url(verification_uri_complete)) { fail(session, L("Login discovery format is invalid.", "登录发现信息格式无效。")); return; }
        snprintf(session->verification_uri_complete, sizeof(session->verification_uri_complete), "%s", verification_uri_complete);
    } else {
    escaped_user_code = curl_easy_escape(NULL, session->user_code, 0);
    if (!escaped_user_code || snprintf(session->verification_uri_complete,
                                      sizeof(session->verification_uri_complete),
                                      "%s?user_code=%s", VerificationUri, escaped_user_code) >=
                                  (int)sizeof(session->verification_uri_complete)) {
        curl_free(escaped_user_code);
        fail(session, "Unable to prepare the verification link.");
        return;
    }
    curl_free(escaped_user_code);
    }
    get_u32(response.data, "interval", &interval);
    session->poll_interval_seconds = interval;
    session->network_retry_seconds = interval > 5 ? interval : 5;
    session->expires_at_ms = now_ms() + (uint64_t)expires * 1000ULL;
    session->next_poll_ms = now_ms() + (uint64_t)interval * 1000ULL;
    session->state = RyuLinkAuth_AwaitingBrowser;
}

void ryuLinkAuthUpdate(RyuLinkAuthSession *session) {
    Response response; long status; char form[MaxToken + 128]; char error[64]; char token_type[32]; char *device; uint32_t expires;
    int form_length;
    if (session->state == RyuLinkAuth_Authenticated) {
        if (now_ms() >= session->expires_at_ms) fail(session, L("Session expired. Start login again.", "会话已过期，请重新登录。"));
        return;
    }
    if (session->state != RyuLinkAuth_AwaitingBrowser || now_ms() < session->next_poll_ms) return;
    if (now_ms() >= session->expires_at_ms) { fail(session, L("Device code expired. Start login again.", "设备代码已过期，请重新登录。")); return; }
    device = curl_easy_escape(NULL, g_device_code, 0);
    if (!device) { fail(session, L("Login request is invalid.", "登录请求无效。")); return; }
    form_length = snprintf(form, sizeof(form),
                           "grant_type=urn:ietf:params:oauth:grant-type:device_code&device_code=%s&client_id=%s&resource=%s",
                           device, ClientId, ResourceFormValue);
    curl_free(device);
    if (form_length < 0 || form_length >= (int)sizeof(form)) { fail(session, L("Login request is invalid.", "登录请求无效。")); return; }
    if (!request(g_token_endpoint, form, NULL, &status, &response)) { schedule_network_retry(session); return; }
    if (status == 200 && get_string(response.data, "access_token", g_access_token, sizeof(g_access_token)) &&
        get_string(response.data, "token_type", token_type, sizeof(token_type)) && !strcasecmp(token_type, "Bearer") &&
        get_u32(response.data, "expires_in", &expires) && expires) {
        session->expires_at_ms = now_ms() + (uint64_t)expires * 1000ULL;
        if (player_me(session)) persist_session(session);
        return;
    }
    if (get_string(response.data, "error", error, sizeof(error))) {
        if (!strcmp(error, "authorization_pending")) { session->network_retry_seconds = session->poll_interval_seconds; session->next_poll_ms = now_ms() + (uint64_t)session->poll_interval_seconds * 1000ULL; return; }
        if (!strcmp(error, "slow_down")) { session->poll_interval_seconds += 5; session->network_retry_seconds = session->poll_interval_seconds; session->next_poll_ms = now_ms() + (uint64_t)session->poll_interval_seconds * 1000ULL; return; }
    }
    if (status == 429 || status == 500 || status == 502 || status == 503 || status == 504) { schedule_network_retry(session); return; }
    fail(session, L("Login was denied or expired.", "登录被拒绝或已过期。"));
}

static bool api_success(const Response *response) {
    const char *code = strstr(response->data, "\"code\"");
    return code && strchr(code, ':') && strtol(strchr(code, ':') + 1, NULL, 10) == 0;
}

static uint32_t api_code(const Response *response) {
    uint32_t code = 0;
    return response && get_u32(response->data, "code", &code) ? code : 0;
}

static bool api_error(RyuLinkAuthSession *session, long status, const char *message) {
    if (status == 401) { fail(session, L("Session expired. Start login again.", "会话已过期，请重新登录。")); return false; }
    if (g_last_curl_result == CURLE_OPERATION_TIMEDOUT) message = L("Request timed out. Please try again.", "请求超时，请重试。");
    snprintf(session->message, sizeof(session->message), "%s", message);
    return false;
}

static bool api_ready(RyuLinkAuthSession *session) {
    if (session && session->state == RyuLinkAuth_Authenticated &&
        (g_has_persisted_session || now_ms() + 60000ULL < session->expires_at_ms)) return true;
    if (session) fail(session, L("Session expired. Start login again.", "会话已过期，请重新登录。"));
    return false;
}

static bool server_device_id_valid(const char *value) {
    if (!value || !memchr(value, '\0', RyuLinkServerDeviceIdBytes) ||
        strlen(value) != RyuLinkServerDeviceIdBytes - 1) return false;
    for (size_t i = 0; i < RyuLinkServerDeviceIdBytes - 1; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-') return false;
        } else if (!isxdigit((unsigned char)value[i])) return false;
    }
    return true;
}

static bool load_or_create_device_identity(RyuLinkAuthSession *session, RyuLinkDeviceIdentity *identity) {
    static const char Hex[] = "0123456789abcdef";
    u8 random[16];
    if (ryuLinkDeviceIdentityLoad(identity)) return true;
    memset(identity, 0, sizeof(*identity));
    randomGet(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); ++i) {
        identity->device_bootstrap_id[i * 2] = Hex[random[i] >> 4];
        identity->device_bootstrap_id[i * 2 + 1] = Hex[random[i] & 0x0f];
    }
    if (ryuLinkDeviceIdentitySave(identity)) return true;
    return api_error(session, 0, L("Unable to save device identity.", "无法保存设备身份。"));
}

bool ryuLinkApiEnsureServerDevice(RyuLinkAuthSession *session, bool force_register,
                                  char out_device_id[37]) {
    RyuLinkDeviceIdentity identity;
    Response response;
    long status;
    char body[128], object[256];
    const char *data;
    if (!api_ready(session) || !out_device_id || !load_or_create_device_identity(session, &identity)) return false;
    if (!force_register && server_device_id_valid(identity.server_device_id)) {
        snprintf(out_device_id, RyuLinkServerDeviceIdBytes, "%s", identity.server_device_id);
        return true;
    }
    if (snprintf(body, sizeof(body), "{\"bootstrapId\":\"%s\"}", identity.device_bootstrap_id) >= (int)sizeof(body) ||
        !request("https://api.ryulink.xyz/app-api/ryulink/devices/register", body, g_access_token, &status, &response) ||
        !api_success(&response)) return api_error(session, status, L("Unable to register this device.", "无法注册此设备。"));
    data = strstr(response.data, "\"data\"");
    if (!data || !next_object(data, object, sizeof(object)) ||
        !get_string(object, "deviceId", identity.server_device_id, sizeof(identity.server_device_id)) ||
        !server_device_id_valid(identity.server_device_id) || !ryuLinkDeviceIdentitySave(&identity))
        return api_error(session, status, L("Invalid device registration response.", "设备注册响应无效。"));
    snprintf(out_device_id, RyuLinkServerDeviceIdBytes, "%s", identity.server_device_id);
    return true;
}

static const char *next_object(const char *cursor, char *object, size_t object_size) {
    const char *start = strchr(cursor, '{'); size_t length = 0; int depth = 0; bool quoted = false;
    if (!start) return NULL;
    for (const char *p = start; *p; ++p) {
        if (*p == '"' && (p == start || p[-1] != '\\')) quoted = !quoted;
        if (!quoted && *p == '{') ++depth;
        if (length + 1 >= object_size) return NULL;
        object[length++] = *p;
        if (!quoted && *p == '}' && --depth == 0) { object[length] = 0; return p + 1; }
    }
    return NULL;
}

static bool parse_room(const char *object, RyuLinkApiRoom *room) {
    uint32_t number, players, capacity;
    memset(room, 0, sizeof(*room));
    if (!get_u64(object, "id", &room->id) || !get_string(object, "displayGameId", room->display_game_id, sizeof(room->display_game_id)) ||
        !get_u32(object, "displayRoomNumber", &number) || number > UINT8_MAX ||
        !get_string(object, "name", room->name, sizeof(room->name)) ||
        !get_string(object, "runtimeStatus", room->runtime_status, sizeof(room->runtime_status)) ||
        !get_u32(object, "onlinePlayerCount", &players) || players > 250 ||
        !get_u32(object, "capacity", &capacity) || capacity == 0 || capacity > 250 ||
        !get_bool(object, "passwordRequired", &room->password_required)) return false;
    room->display_room_number = (uint8_t)number;
    room->online_player_count = (uint16_t)players;
    room->capacity = (uint16_t)capacity;
    if (!get_string(object, "roomType", room->type, sizeof(room->type)) &&
        !get_string(object, "type", room->type, sizeof(room->type))) {
        room->type[0] = '\0';
    }
    get_string(object, "ownerDisplayName", room->owner_display_name, sizeof(room->owner_display_name));
    get_bool(object, "isOwner", &room->is_owner);
    return true;
}

bool ryuLinkApiListRooms(RyuLinkAuthSession *session, const char *type,
                         const char *display_game_id, RyuLinkApiRoomPage *page) {
    char url[320], object[1024]; Response response; long status; const char *cursor;
    int written;
    if (!api_ready(session) || !page || !type) return false;
    if (display_game_id && display_game_id[0]) {
        written = snprintf(url, sizeof(url),
                           "https://api.ryulink.xyz/app-api/ryulink/rooms?roomType=%s&displayGameId=%s&pageNo=1&pageSize=%d",
                           type, display_game_id, RyuLinkApiRoomLimit);
    } else {
        written = snprintf(url, sizeof(url),
                           "https://api.ryulink.xyz/app-api/ryulink/rooms?roomType=%s&pageNo=1&pageSize=%d",
                           type, RyuLinkApiRoomLimit);
    }
    if (written < 0 || written >= (int)sizeof(url)) return false;
    memset(page, 0, sizeof(*page));
    if (!app_request(url, "GET", NULL, &status, &response) || !api_success(&response)) return api_error(session, status, "Unable to load rooms.");
    cursor = strstr(response.data, "\"list\"");
    if (!cursor || !(cursor = strchr(cursor, '['))) return api_error(session, status, "Invalid room response.");
    ++cursor;
    while (page->count < RyuLinkApiRoomLimit && (cursor = next_object(cursor, object, sizeof(object)))) {
        if (!parse_room(object, &page->rooms[page->count++])) return api_error(session, status, "Invalid room response.");
        while (isspace((unsigned char)*cursor) || *cursor == ',') ++cursor;
        if (*cursor == ']') break;
    }
    get_u32(response.data, "total", &page->total);
    return true;
}

bool ryuLinkApiGetRoom(RyuLinkAuthSession *session, uint64_t room_id, RyuLinkApiRoom *room) {
    char url[160], object[2048]; Response response; long status; const char *data;
    if (!api_ready(session) || !room || snprintf(url, sizeof(url), "https://api.ryulink.xyz/app-api/ryulink/rooms/%llu", (unsigned long long)room_id) >= (int)sizeof(url)) return false;
    if (!app_request(url, "GET", NULL, &status, &response) || !api_success(&response)) return api_error(session, status, "Unable to load room details.");
    data = strstr(response.data, "\"data\"");
    if (!data || !next_object(data, object, sizeof(object)) || !parse_room(object, room)) return api_error(session, status, "Invalid room response.");
    return true;
}

bool ryuLinkApiListNodes(RyuLinkAuthSession *session, RyuLinkApiNode *nodes, uint8_t *count) {
    Response response; long status; const char *cursor; char object[512];
    if (!api_ready(session) || !nodes || !count) return false;
    *count = 0;
    if (!app_request("https://api.ryulink.xyz/app-api/ryulink/nodes", "GET", NULL, &status, &response) || !api_success(&response)) return api_error(session, status, "Unable to load nodes.");
    cursor = strstr(response.data, "\"data\"");
    if (!cursor || !(cursor = strchr(cursor, '['))) return api_error(session, status, "Invalid node response.");
    for (++cursor; *count < RyuLinkApiNodeLimit && (cursor = next_object(cursor, object, sizeof(object))); ++*count) {
        RyuLinkApiNode *node = &nodes[*count]; memset(node, 0, sizeof(*node));
        if (!get_string(object, "id", node->id, sizeof(node->id)) || !get_string(object, "name", node->name, sizeof(node->name)) ||
            !get_bool(object, "available", &node->available) || !get_bool(object, "isPreferred", &node->preferred)) return api_error(session, status, "Invalid node response.");
    }
    return true;
}

bool ryuLinkApiSetPreferredNode(RyuLinkAuthSession *session, const char *node_id) {
    char body[100]; Response response; long status;
    if (!api_ready(session) || !node_id || !node_id[0] || strchr(node_id, '"') ||
        snprintf(body, sizeof(body), "{\"nodeId\":\"%s\"}", node_id) >= (int)sizeof(body)) return false;
    if (!app_request("https://api.ryulink.xyz/app-api/ryulink/nodes/preferred", "PUT", body, &status, &response) || !api_success(&response)) return api_error(session, status, "Unable to save node preference.");
    return true;
}

bool ryuLinkApiJoinRoom(RyuLinkAuthSession *session, uint64_t room_id, const char *device_id,
                        const char *server_device_id, const char *password, RyuLinkApiJoin *join) {
    char url[160], body[384], object[4096]; Response response; long status; const char *data;
    int body_len;
    if (!api_ready(session) || !join || !device_id || !device_id[0] || strchr(device_id, '"') ||
        !server_device_id_valid(server_device_id) ||
        (password && strchr(password, '"')) ||
        snprintf(url, sizeof(url), "https://api.ryulink.xyz/app-api/ryulink/rooms/%llu/join", (unsigned long long)room_id) >= (int)sizeof(url))
        return false;
    if (password && password[0]) {
        body_len = snprintf(body, sizeof(body), "{\"deviceId\":\"%s\",\"serverDeviceId\":\"%s\",\"password\":\"%s\"}", device_id, server_device_id, password);
    } else {
        body_len = snprintf(body, sizeof(body), "{\"deviceId\":\"%s\",\"serverDeviceId\":\"%s\"}", device_id, server_device_id);
    }
    if (body_len < 0 || body_len >= (int)sizeof(body)) return false;
    memset(join, 0, sizeof(*join));
    session->device_not_registered = false;
    if (!app_request(url, "POST", body, &status, &response) || !api_success(&response)) {
        uint32_t code = api_code(&response);
        if (code == 42602) {
            session->device_not_registered = true;
            return api_error(session, status, L("DEVICE REGISTRATION EXPIRED", "设备注册已失效"));
        }
        if (code == 40901) return api_error(session, status, L("ROOM IS FULL", "网络空间已满"));
        if (code == 40301) return api_error(session, status, L("NO TEST ACCESS", "无测试资格"));
        if (code == 40302) {
            session->needs_vip = true;
            return api_error(session, status,
                             L("VIP MEMBERSHIP REQUIRED TO JOIN", "需要开通 VIP 会员后才能加入 VIP 区"));
        }
        if (code == 40303) return api_error(session, status, L("WRONG PASSWORD", "密码错误"));
        if (code == 40401) return api_error(session, status, L("ROOM NOT AVAILABLE", "网络空间不可用"));
        if (code == 50301) return api_error(session, status, L("ROOM CONNECTION UNAVAILABLE", "房间联机暂不可用"));
        if (code == 50302) return api_error(session, status, L("VIRTUAL IP POOL EXHAUSTED", "虚拟 IP 地址池已耗尽"));
        if (code == 40902) return api_error(session, status, L("ROOM IS STARTING. TRY AGAIN SOON", "正在启动，请稍后重试"));
        return api_error(session, status, L("UNABLE TO JOIN ROOM", "无法加入网络空间"));
    }
    data = strstr(response.data, "\"data\"");
    if (!data || !next_object(data, object, sizeof(object)) || !get_u64(object, "roomId", &join->room_id) ||
        !get_string(object, "virtualIp", join->virtual_ip, sizeof(join->virtual_ip)))
        return api_error(session, status, "UNABLE TO JOIN ROOM");
    get_string(object, "roomName", join->room_name, sizeof(join->room_name));
    return true;
}

bool ryuLinkApiLeaveRoom(RyuLinkAuthSession *session, uint64_t room_id) {
    char url[160]; Response response; long status;
    if (!api_ready(session) ||
        snprintf(url, sizeof(url), "https://api.ryulink.xyz/app-api/ryulink/rooms/%llu/leave", (unsigned long long)room_id) >= (int)sizeof(url)) return false;
    if (!app_request(url, "POST", "{}", &status, &response) || !api_success(&response)) {
        uint32_t code = api_code(&response);
        return api_error(session, status, "UNABLE TO LEAVE ROOM");
    }
    return true;
}

bool ryuLinkApiHeartbeatRoom(RyuLinkAuthSession *session, uint64_t room_id) {
    char url[160]; Response response; long status;
    if (!api_ready(session) ||
        snprintf(url, sizeof(url), "https://api.ryulink.xyz/app-api/ryulink/rooms/%llu/heartbeat",
                 (unsigned long long)room_id) >= (int)sizeof(url))
        return false;
    /* Soft-fail: stale/missing membership must not clear local room_selection. */
    if (!app_request(url, "POST", "{}", &status, &response) || !api_success(&response)) {
        return false;
    }
    return true;
}

bool ryuLinkApiUploadEvidence(RyuLinkAuthSession *session, const char *path,
                              RyuLinkApiEvidenceUpload *upload) {
    char authorization[MaxToken + 32];
    Response response;
    long status = 0;
    if (!api_ready(session) || path == NULL || !path[0] || upload == NULL ||
        !g_access_token[0]) return false;
    memset(upload, 0, sizeof(*upload));
    session->message[0] = '\0';
    if (snprintf(authorization, sizeof(authorization), "Authorization: Bearer %s",
                 g_access_token) >= (int)sizeof(authorization)) return false;

    ryuLinkLoadingBegin();
    for (int attempt = 0; attempt < 2; ++attempt) {
        CURL *curl = curl_easy_init();
        curl_mime *mime = NULL;
        curl_mimepart *part = NULL;
        struct curl_slist *headers = NULL;
        struct curl_slist *resolve = NULL;
        if (curl == NULL) {
            ryuLinkLoadingEnd();
            return api_error(session, 0,
                L("UPLOAD COULD NOT START", "无法启动上传"));
        }
        memset(&response, 0, sizeof(response));
        status = 0;
        mime = curl_mime_init(curl);
        part = mime ? curl_mime_addpart(mime) : NULL;
        if (part == NULL || curl_mime_name(part, "evidence") != CURLE_OK ||
            curl_mime_filename(part, "ryulink-diagnostic-evidence.txt") != CURLE_OK ||
            curl_mime_type(part, "text/plain") != CURLE_OK ||
            curl_mime_filedata(part, path) != CURLE_OK) {
            curl_mime_free(mime);
            curl_easy_cleanup(curl);
            ryuLinkLoadingEnd();
            return api_error(session, 0,
                L("EVIDENCE FILE COULD NOT BE READ", "无法读取诊断证据文件"));
        }
        headers = curl_slist_append(headers, authorization);
        curl_easy_setopt(curl, CURLOPT_URL, DiagnosticEvidenceUrl);
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, TimeoutSeconds);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, TimeoutSeconds);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, RYULINK_APP_USER_AGENT);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        if (attempt == 1) {
            resolve = curl_slist_append(NULL, ApiDnsFallback);
            curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
        }
        g_last_curl_result = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(resolve);
        curl_slist_free_all(headers);
        curl_mime_free(mime);
        curl_easy_cleanup(curl);
        if (g_last_curl_result == CURLE_COULDNT_RESOLVE_HOST && attempt == 0) continue;
        break;
    }
    if (g_last_curl_result != CURLE_OK || response.too_large) {
        ryuLinkLoadingEnd();
        return api_error(session, status,
            L("UPLOAD FAILED - CHECK NETWORK", "上传失败，请检查网络"));
    }
    if (!api_success(&response)) {
        uint32_t code = api_code(&response);
        if (status == 413 || code == 41301) { ryuLinkLoadingEnd(); return api_error(session, status,
            L("DIAGNOSTIC EVIDENCE IS INVALID OR TOO LARGE", "诊断证据格式无效或超过大小限制"));
        }
        if (status == 429 || code == 42901) { ryuLinkLoadingEnd(); return api_error(session, status,
            L("UPLOAD TOO FREQUENT - TRY AGAIN LATER", "上传过于频繁，请稍后再试"));
        }
        if (code == 50320) { ryuLinkLoadingEnd(); return api_error(session, status,
            L("DIAGNOSTIC UPLOAD IS TEMPORARILY DISABLED", "诊断上传暂未开放"));
        }
        ryuLinkLoadingEnd();
        return api_error(session, status,
            L("DIAGNOSTIC UPLOAD FAILED", "诊断证据上传失败"));
    }
    if (!get_string(response.data, "evidenceId", upload->evidence_id,
                    sizeof(upload->evidence_id)) ||
        !evidence_id_valid(upload->evidence_id)) {
        ryuLinkLoadingEnd();
        return api_error(session, status,
            L("INVALID UPLOAD RESPONSE", "上传响应无效"));
    }
    (void)get_u32(response.data, "storedBytes", &upload->stored_bytes);
    ryuLinkLoadingEnd();
    return true;
}
