#include "app.h"
#include "device_identity_store.h"
#include "ldn_mitm_ipc.h"
#include "localization.h"
#include "network_profile.h"
#include "qrcodegen.h"
#include "room_selection_store.h"
#include "virtual_ip.h"

#include <stdio.h>
#include <string.h>

#define L(english, chinese) ryuLinkLocalize((english), (chinese))

enum {
    ColorBackground = RGBA8_MAXALPHA(16, 18, 24), ColorPanel = RGBA8_MAXALPHA(27, 31, 42),
    ColorPanelMuted = RGBA8_MAXALPHA(35, 40, 54), ColorPrimary = RGBA8_MAXALPHA(0, 200, 255),
    ColorAccent = RGBA8_MAXALPHA(123, 97, 255), ColorText = RGBA8_MAXALPHA(242, 246, 252),
    ColorMuted = RGBA8_MAXALPHA(151, 165, 184), ColorGood = RGBA8_MAXALPHA(66, 218, 151),
    ColorBusy = RGBA8_MAXALPHA(255, 195, 70),
};

static uint8_t g_qr_temp[qrcodegen_BUFFER_LEN_FOR_VERSION(20)];
static uint8_t g_qr_code[qrcodegen_BUFFER_LEN_FOR_VERSION(20)];
static char g_qr_payload[4097];
static bool g_qr_ready;

static u64 current_ms(void) { return armTicksToNs(armGetSystemTick()) / 1000000ULL; }

static void draw_header(const char *title, const char *subtitle) {
    ryuLinkUiText(64, 45, 5, ColorText, L("RYULINK", "龙联")); ryuLinkUiText(64, 98, 2, ColorMuted, subtitle);
    ryuLinkUiText(1000, 58, 2, ColorPrimary, title); ryuLinkUiRect(64, 135, 1152, 2, ColorPanelMuted);
}

static void draw_navigation(RyuLinkPage page) {
    const char *labels[] = {L("L LOBBY", "L 大厅"), L("Y MY", "Y 我的"), L("R SETTINGS", "R 设置")};
    const RyuLinkPage pages[] = {RyuLinkPage_Home, RyuLinkPage_Profile, RyuLinkPage_Settings};
    ryuLinkUiRect(0, 650, RyuLinkScreenWidth, 70, RGBA8_MAXALPHA(12, 14, 19));
    for (int i = 0, x = 400; i < 3; ++i, x += 190) {
        if (page == pages[i]) ryuLinkUiRoundedPanel(x - 15, 666, 170, 36, 8, ColorPanelMuted);
        ryuLinkUiText(x, 677, 2, page == pages[i] ? ColorPrimary : ColorMuted, labels[i]);
    }
}

static bool update_qr_code(const char *payload) {
    if (!payload[0]) return false;
    if (g_qr_ready && !strcmp(g_qr_payload, payload)) return true;
    g_qr_ready = qrcodegen_encodeText(payload, g_qr_temp, g_qr_code, qrcodegen_Ecc_MEDIUM,
                                       qrcodegen_VERSION_MIN, 20, qrcodegen_Mask_AUTO, true);
    if (g_qr_ready) snprintf(g_qr_payload, sizeof(g_qr_payload), "%s", payload);
    return g_qr_ready;
}

static void draw_qr_code(int x, int y, int available_size) {
    int size = qrcodegen_getSize(g_qr_code);
    int module_size = available_size / size;
    int rendered_size;

    if (module_size < 1) return;
    rendered_size = size * module_size;
    ryuLinkUiRect(x - 12, y - 12, rendered_size + 24, rendered_size + 24, ColorText);
    for (int row = 0; row < size; ++row) {
        for (int column = 0; column < size; ++column) {
            if (qrcodegen_getModule(g_qr_code, column, row)) {
                ryuLinkUiRect(x + column * module_size, y + row * module_size,
                              module_size, module_size, ColorBackground);
            }
        }
    }
}

static const char *format_user_code(const char *user_code, char formatted[129]) {
    size_t length = strlen(user_code);
    size_t midpoint = length / 2;

    if (length < 2 || strchr(user_code, '-')) return user_code;
    memcpy(formatted, user_code, midpoint);
    formatted[midpoint] = '-';
    memcpy(formatted + midpoint + 1, user_code + midpoint, length - midpoint + 1);
    return formatted;
}

static void draw_splash(void) {
    ryuLinkUiCenteredText(230, 9, ColorText, L("RYULINK", "龙联"));
    ryuLinkUiCenteredText(336, 3, ColorPrimary, L("ENTER A NETWORK SPACE", "进入网络空间，一起游玩"));
    ryuLinkUiRoundedPanel(488, 478, 304, 64, 12, ColorAccent); ryuLinkUiCenteredText(498, 3, ColorText, L("A CONTINUE", "A 继续"));
}

static const char *space_type_api(RyuLinkSpaceKind kind) {
    if (kind == RyuLinkSpace_Vip) return "VIP";
    if (kind == RyuLinkSpace_Private) return "PRIVATE";
    return "PUBLIC";
}

static const char *room_type_label(const char *room_type) {
    if (room_type && !strcmp(room_type, "VIP")) return L("VIP ZONE", "VIP区");
    if (room_type && !strcmp(room_type, "PRIVATE")) return L("PRIVATE SERVER", "私服");
    return L("PUBLIC ZONE", "公共区");
}

static bool prompt_password(char *out, size_t out_size) {
    SwkbdConfig conf;
    Result rc;
    if (!out || out_size < 2) return false;
    out[0] = '\0';
    rc = swkbdCreate(&conf, 0);
    if (R_FAILED(rc)) return false;
    swkbdConfigMakePresetPassword(&conf);
    swkbdConfigSetGuideText(&conf, "Room password");
    swkbdConfigSetOkButtonText(&conf, "OK");
    rc = swkbdShow(&conf, out, out_size);
    swkbdClose(&conf);
    return R_SUCCEEDED(rc) && out[0] != '\0';
}

static void draw_login(const RyuLinkApp *app) {
    const RyuLinkAuthSession *auth = &app->auth;
    char formatted_user_code[129];
    draw_header(L("LOGIN", "登录"), L("SIGN IN WITH YOUR PHONE", "使用手机登录")); ryuLinkUiRoundedPanel(160, 180, 960, 410, 18, ColorPanel);
    if (auth->state == RyuLinkAuth_AwaitingBrowser) {
        if (update_qr_code(auth->verification_uri_complete)) draw_qr_code(225, 225, 280);
        ryuLinkUiText(225, 525, 4, ColorPrimary, format_user_code(auth->user_code, formatted_user_code));
        ryuLinkUiText(600, 245, 3, ColorText, L("SCAN WITH YOUR PHONE", "使用手机扫码"));
        ryuLinkUiText(600, 305, 2, ColorMuted, L("OR OPEN ON YOUR PHONE", "或在手机上打开"));
        ryuLinkUiText(600, 345, 2, ColorPrimary, L("AUTH RYULINK XYZ DEVICE", "AUTH RYULINK XYZ DEVICE"));
        ryuLinkUiText(600, 410, 2, ColorMuted, L("ENTER THE CODE BELOW THE QR", "输入二维码下方的短码"));
        ryuLinkUiText(600, 470, 2, ColorBusy, L("WAITING FOR AUTHORIZATION", "等待授权"));
        ryuLinkUiText(600, 530, 2, ColorMuted, L("B CANCEL", "B 取消"));
    } else if (auth->state == RyuLinkAuth_Error) {
        ryuLinkUiText(320, 270, 3, ColorAccent, L("LOGIN COULD NOT START", "无法开始登录"));
        ryuLinkUiText(320, 350, 2, ColorText, auth->message[0] ? auth->message : L("PLEASE TRY AGAIN", "请重试"));
        ryuLinkUiText(320, 520, 2, ColorPrimary, L("A TRY AGAIN  B BACK", "A 重试  B 返回"));
    } else {
        ryuLinkUiText(320, 250, 4, ColorText, L("SIGN IN WITH YOUR PHONE", "使用手机登录"));
        ryuLinkUiText(320, 345, 2, ColorMuted, L("A DEVICE CODE LOGIN", "A 设备代码登录"));
        ryuLinkUiText(320, 390, 2, ColorMuted, L("THIS DEVICE STAYS SIGNED IN", "此设备将保持登录状态"));
        ryuLinkUiText(320, 520, 2, ColorPrimary, L("A START LOGIN", "A 开始登录"));
    }
}

static u32 room_color(const RyuLinkApiRoom *room) {
    if (!strcmp(room->runtime_status, "PLAYING")) return ColorBusy;
    if (!strcmp(room->runtime_status, "UNAVAILABLE") || room->online_player_count >= room->capacity) return ColorAccent;
    return ColorGood;
}

static const char *room_status(const char *status) {
    if (!strcmp(status, "PLAYING")) return L("PLAYING", "游戏中");
    if (!strcmp(status, "UNAVAILABLE")) return L("UNAVAILABLE", "不可用");
    return status;
}

static void draw_home(const RyuLinkApp *app) {
    bool no_computer = app->relay_mode == RyuLinkRelayMode_NoComputerBeta;

    draw_header(L("LOBBY", "大厅"), L("CHOOSE A RELAY MODE", "选择中继方式"));
    ryuLinkUiRoundedPanel(64, 178, 245, 430, 16, ColorPanel);
    ryuLinkUiText(92, 204, 2, ColorPrimary, L("RELAY MODE", "中继方式"));
    if (no_computer) ryuLinkUiRoundedPanel(80, 248, 213, 58, 9, ColorAccent);
    ryuLinkUiText(96, 266, 2, no_computer ? ColorText : ColorMuted, L("1 NO-PC BETA", "1 免电脑 beta"));
    if (!no_computer) ryuLinkUiRoundedPanel(80, 328, 213, 58, 9, ColorAccent);
    ryuLinkUiText(96, 346, 2, no_computer ? ColorMuted : ColorText, L("2 COMPUTER RELAY", "2 电脑中继"));

    ryuLinkUiRoundedPanel(335, 178, 881, 430, 16, ColorPanel);
    if (no_computer) {
        u32 mtu_color = app->network_mtu_known && app->network_mtu_is_1500 ? ColorGood : ColorPanelMuted;
        if (update_qr_code("https://account.ryulink.xyz/ryulink/account")) {
            draw_qr_code(1040, 210, 132);
            ryuLinkUiText(1046, 370, 2, ColorMuted, L("SCAN FOR VIP", "扫码获取VIP"));
        }
        ryuLinkUiText(390, 225, 3, ColorPrimary, L("NO-PC BETA", "免电脑 beta"));
        ryuLinkUiText(390, 300, 2, ColorText, L("START RELAY WITH YOUR VIRTUAL IP", "使用虚拟 IP 开启中继"));
        ryuLinkUiText(390, 345, 2, ColorMuted,
                      L("NO COMPUTER RELAY IS REQUIRED", "无需使用电脑中继"));
        ryuLinkUiRoundedPanel(390, 438, 180, 72, 12, mtu_color);
        ryuLinkUiText(416, 460, 2, ColorText, L("MTU: 1500", "MTU: 1500"));
        ryuLinkUiText(413, 484, 2, ColorText, L("X SET MTU", "X 设置 MTU"));
        ryuLinkUiRoundedPanel(590, 438, 340, 72, 12, ColorAccent);
        ryuLinkUiText(670, 463, 3, ColorText, L("A START RELAY", "A 开启中继"));
        if (app->join_message[0])
            ryuLinkUiText(390, 540, 2, ColorGood, app->join_message);
    } else {
        ryuLinkUiText(390, 225, 3, ColorPrimary, L("COMPUTER RELAY", "电脑中继"));
        ryuLinkUiCenteredText(350, 3, ColorMuted, L("NOT PLANNED", "暂不开发"));
    }
    ryuLinkUiText(390, 568, 2, ColorMuted, L("DPAD MOVE", "十字键切换"));
    draw_navigation(RyuLinkPage_Home);
}

static void draw_room_detail(const RyuLinkApp *app) {
    const RyuLinkApiRoom *room = &app->room_detail; char players[32];
    bool same_selected = app->room_selection.active && app->room_selection.room_id == room->id;
    bool vip_room = room->type[0] && !strcmp(room->type, "VIP");
    snprintf(players, sizeof(players), L("%u / %u IN SPACE", "%u / %u 人在网"), room->online_player_count, room->capacity);
    draw_header(L("NETWORK SPACE", "网络空间"),
                same_selected ? L("X LEAVE SELECTION  B RETURN TO LOBBY", "X 离开选房  B 返回大厅")
                              : (app->vip_upsell_pending
                                     ? L("A OPEN MEMBERSHIP  B BACK", "A 立即开通  B 返回")
                                     : (room->password_required
                                            ? L("A JOIN WITH PASSWORD  B BACK", "A 输入密码加入  B 返回")
                                            : L("A JOIN SPACE  B RETURN TO LOBBY", "A 加入网络空间  B 返回大厅"))));
    ryuLinkUiRoundedPanel(170, 182, 940, 405, 18, ColorPanel);
    ryuLinkUiText(220, 222, 2, ColorMuted, L("ROOM TYPE", "房间类型"));
    ryuLinkUiText(220, 262, 3, ColorPrimary, room_type_label(room->type));
    if (vip_room) ryuLinkUiText(520, 268, 2, ColorBusy, L("VIP ZONE", "VIP区"));
    ryuLinkUiText(220, 320, 4, ColorText, room->name);
    ryuLinkUiText(720, 342, 2, room_color(room), room_status(room->runtime_status));
    ryuLinkUiText(220, 390, 2, ColorMuted, L("NETWORK MEMBERS", "网络人数"));
    ryuLinkUiText(570, 390, 2, ColorText, players);
    if (room->owner_display_name[0]) ryuLinkUiText(220, 440, 2, ColorMuted, room->owner_display_name);
    if (room->password_required && !app->vip_upsell_pending) {
        ryuLinkUiText(220, 470, 2, ColorBusy, L("PASSWORD REQUIRED", "需要密码"));
    }
    if (app->vip_upsell_pending) {
        ryuLinkUiText(220, 470, 2, ColorBusy,
                      L("VIP ZONE - MEMBERSHIP REQUIRED", "该房间为 VIP 专属区域，请开通会员后使用"));
        ryuLinkUiText(220, 520, 2, ColorPrimary, L("A OPEN MEMBERSHIP", "A 立即开通"));
    } else {
        ryuLinkUiText(220, 505, 2, app->join_message[0] ? (same_selected ? ColorGood : ColorBusy) : ColorPrimary,
                      app->join_message[0] ? app->join_message
                                           : (same_selected ? L("SPACE ALREADY SELECTED", "已选择此网络空间")
                                                           : L("A JOIN NETWORK SPACE", "A 加入网络空间")));
    }
    ryuLinkUiText(220, 560, 2, ColorAccent, L("B RETURN TO LOBBY", "B 返回大厅"));
}

static void draw_profile(const RyuLinkApp *app) {
    const RyuLinkAuthSession *auth = &app->auth;
    draw_header(L("MY PROFILE", "我的资料"), L("LIVE PLAYER DATA AND NODE PREFERENCE", "玩家信息和节点偏好")); ryuLinkUiRoundedPanel(210, 170, 860, 440, 18, ColorPanel);
    ryuLinkUiText(265, 202, 3, ColorPrimary, auth->display_name[0] ? auth->display_name : L("PLAYER", "玩家"));
    ryuLinkUiText(265, 248, 2, ColorMuted, L("RYULINK ID", "龙联 ID"));
    ryuLinkUiText(620, 248, 2, ColorText, auth->player_code[0] ? auth->player_code : "-");
    ryuLinkUiText(265, 295, 2, ColorMuted, L("MEMBERSHIP", "会员"));
    ryuLinkUiText(620, 295, 2, auth->vip_active ? ColorPrimary : ColorText,
                  auth->vip_active ? L("VIP ACTIVE", "VIP 已激活") : L("NO VIP", "无 VIP"));
    if (!auth->vip_active) {
        ryuLinkUiText(265, 325, 2, ColorBusy,
                      L("OPEN VIP ON ACCOUNT RYULINK XYZ", "请在 account.ryulink.xyz 开通 VIP"));
    }
    ryuLinkUiText(265, 355, 2, ColorMuted, L("TEST ACCESS", "测试权限"));
    ryuLinkUiText(620, 355, 2,
                  auth->account_disabled ? ColorAccent : auth->test_access_enabled ? ColorGood : ColorMuted,
                  auth->account_disabled ? L("ACCOUNT DISABLED", "账号已禁用")
                                        : auth->test_access_enabled ? L("ENABLED", "已启用")
                                                                    : L("NOT GRANTED", "未授予"));
    if (auth->private_room_name[0]) {
        ryuLinkUiText(265, 400, 2, ColorMuted, L("PRIVATE SERVER", "私服"));
        ryuLinkUiText(620, 400, 2, ColorText, auth->private_room_name);
    }
    ryuLinkUiText(265, 430, 2, app->profile_action == 0 ? ColorPrimary : ColorMuted, L("PREFERRED NODE", "首选节点"));
    ryuLinkUiText(620, 430, 2, ColorText, app->node_count ? app->nodes[app->selected_node].name : L("NO NODES AVAILABLE", "没有可用节点"));
    ryuLinkUiText(265, 490, 2, app->profile_action == 1 ? ColorPrimary : ColorMuted, L("LOGOUT", "退出登录")); ryuLinkUiText(620, 490, 2, ColorAccent, L("CLEAR IN MEMORY SESSION", "清除内存中的会话"));
    ryuLinkUiText(265, 550, 2, ColorMuted, L("DPAD MOVE  LR NODE  A SELECT", "十字键移动  LR 节点  A 选择")); draw_navigation(RyuLinkPage_Profile);
}

static void draw_settings(void) {
    draw_header(L("SETTINGS", "设置"), L("APP PREFERENCES", "应用偏好"));
    ryuLinkUiRoundedPanel(210, 192, 860, 370, 18, ColorPanel);
    ryuLinkUiText(265, 245, 2, ColorMuted, L("SECURITY", "安全"));
    ryuLinkUiText(610, 245, 2, ColorText, L("HTTPS VERIFIED", "HTTPS 已验证"));
    ryuLinkUiText(265, 330, 2, ColorMuted, L("ROOM JOIN", "加入房间"));
    ryuLinkUiText(610, 330, 2, ColorGood, L("READY", "就绪"));
    draw_navigation(RyuLinkPage_Settings);
}

static bool refresh_rooms(RyuLinkApp *app) {
    app->selected_room = 0;
    app->auth.message[0] = '\0';
    /* List by roomType only; do not filter by displayGameId (game category). */
    return ryuLinkApiListRooms(&app->auth, space_type_api(app->space_kind), NULL, &app->room_page);
}

static void refresh_network_mtu(RyuLinkApp *app) {
    NifmNetworkProfileData profile;
    app->network_mtu_known = R_SUCCEEDED(ryuLinkNetworkProfileReadCurrent(&profile));
    app->network_mtu_is_1500 = app->network_mtu_known && profile.ip_setting_data.mtu == 1500;
}

static void enter_lobby(RyuLinkApp *app) {
    ryuLinkApiListNodes(&app->auth, app->nodes, &app->node_count);
    for (uint8_t i = 0; i < app->node_count; ++i) if (app->nodes[i].preferred) { app->selected_node = i; break; }
    refresh_network_mtu(app);
    app->page = RyuLinkPage_Home;
}

void ryuLinkAppInitialize(RyuLinkApp *app) {
    static const char Hex[] = "0123456789abcdef";
    u8 random[16];
    RyuLinkStoredRoomSelection stored;
    memset(app, 0, sizeof(*app)); app->page = RyuLinkPage_Splash; app->type_list_focused = true; app->relay_mode = RyuLinkRelayMode_NoComputerBeta; app->splash_started_ms = current_ms();
    app->pending = RyuLinkPending_None; app->pending_target = RyuLinkPage_Login;
    randomGet(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); ++i) { app->device_id[i * 2] = Hex[random[i] >> 4]; app->device_id[i * 2 + 1] = Hex[random[i] & 0x0f]; }
    if (ryuLinkRoomSelectionLoad(&stored)) {
        app->room_selection.active = true;
        app->room_selection.room_id = stored.room_id;
        app->room_selection.revision = stored.revision;
        snprintf(app->room_selection.room_name, sizeof(app->room_selection.room_name), "%s", stored.room_name);
        snprintf(app->join_message, sizeof(app->join_message), "%s", L("PREVIOUS ROOM STILL SELECTED", "仍保持上次选择的房间"));
    }
}

static void refresh_selection_from_control_plane(RyuLinkApp *app);

void ryuLinkAppUpdate(RyuLinkApp *app) {
    /* Control-plane presence only; sysmodule does not heartbeat. Soft-fail keeps local selection. */
    const u64 interval_ms = 90000ULL;
    u64 now;
    if (!app) return;
    now = current_ms();
    if (app->network_mtu_sync_pending && now >= app->network_mtu_sync_after_ms) {
        app->network_mtu_sync_pending = false;
        refresh_network_mtu(app);
    }
    if (app->auth.state != RyuLinkAuth_Authenticated || !app->room_selection.active) return;
    if (!app->selection_applied_this_boot) {
        app->selection_applied_this_boot = true;
        refresh_selection_from_control_plane(app);
    }
    if (app->last_heartbeat_ms != 0 && now - app->last_heartbeat_ms < interval_ms) return;
    app->last_heartbeat_ms = now;
    (void)ryuLinkApiHeartbeatRoom(&app->auth, app->room_selection.room_id);
}

static void remember_room_selection(RyuLinkApp *app, const RyuLinkApiJoin *join) {
    RyuLinkStoredRoomSelection stored;
    uint64_t revision = app->room_selection.revision + 1;
    memset(&app->room_selection, 0, sizeof(app->room_selection));
    app->room_selection.active = true;
    app->room_selection.revision = revision;
    app->room_selection.room_id = join->room_id;
    snprintf(app->room_selection.room_name, sizeof(app->room_selection.room_name), "%s", join->room_name);

    memset(&stored, 0, sizeof(stored));
    stored.active = true;
    stored.room_id = join->room_id;
    stored.revision = revision;
    snprintf(stored.room_name, sizeof(stored.room_name), "%s", join->room_name);
    ryuLinkRoomSelectionSave(&stored);
}

static bool configure_virtual_ip(RyuLinkApp *app) {
    char server_device_id[RyuLinkServerDeviceIdBytes];
    char virtual_ip[16];
    uint32_t ip;

    if (!ryuLinkApiEnsureServerDevice(&app->auth, false, server_device_id)) return false;
    if (!ryuLinkApiGetVirtualIp(&app->auth, server_device_id, virtual_ip)) {
        if (!app->auth.device_not_registered ||
            !ryuLinkApiEnsureServerDevice(&app->auth, true, server_device_id) ||
            !ryuLinkApiGetVirtualIp(&app->auth, server_device_id, virtual_ip)) return false;
    }
    if (!ryuLinkParseVirtualIp(virtual_ip, &ip)) {
        snprintf(app->join_message, sizeof(app->join_message), "%s",
                 L("SERVER RETURNED INVALID VIRTUAL IP", "服务端返回的虚拟 IP 无效"));
        return false;
    }
    if (!ryuLinkLdnMitmIpcSetVirtualIp(ip)) {
        (void)ryuLinkLdnMitmIpcSetInternetRelayEnabled(false);
        snprintf(app->join_message, sizeof(app->join_message), "%s",
                 L("LDN_MITM RELAY SETUP FAILED", "LDN_MITM 中继设置失败"));
        return false;
    }
    return true;
}

static bool configure_relay_credential(RyuLinkApp *app) {
    char server_device_id[RyuLinkServerDeviceIdBytes];
    char credential[RyuLinkRelayCredentialBytes];

    if (!ryuLinkApiEnsureServerDevice(&app->auth, false, server_device_id) ||
        !ryuLinkApiGetRelayCredential(&app->auth, server_device_id, credential) ||
        !ryuLinkLdnMitmIpcSetRelayCredential(credential)) {
        memset(credential, 0, sizeof(credential));
        (void)ryuLinkLdnMitmIpcSetInternetRelayEnabled(false);
        snprintf(app->join_message, sizeof(app->join_message), "%s",
                 app->auth.message[0] ? app->auth.message : L("RELAY CREDENTIAL SETUP FAILED", "中继凭证设置失败"));
        return false;
    }
    memset(credential, 0, sizeof(credential));
    return true;
}

static bool require_relay_membership(RyuLinkApp *app) {
    if (app->auth.vip_active) return true;
    (void)ryuLinkLdnMitmIpcSetInternetRelayEnabled(false);
    (void)ryuLinkLdnMitmIpcSetRelayCredential(NULL);
    app->vip_upsell_pending = true;
    snprintf(app->join_message, sizeof(app->join_message), "%s",
             L("VIP REQUIRED - SCAN QR TO ACTIVATE", "需要 VIP 会员，请扫码开通"));
    return false;
}

static bool enable_relay(RyuLinkApp *app) {
    if (!require_relay_membership(app)) return false;
    if (!configure_relay_credential(app)) return false;
    if (ryuLinkLdnMitmIpcSetInternetRelayEnabled(true)) return true;
    (void)ryuLinkLdnMitmIpcSetInternetRelayEnabled(false);
    snprintf(app->join_message, sizeof(app->join_message), "%s",
             L("LDN_MITM RELAY SETUP FAILED", "LDN_MITM 中继设置失败"));
    return false;
}

/* Runs after each successful App sign-in. The server device retains its
 * assigned virtual IP, while the Core retains the last value for game-only
 * launches. A non-member is explicitly left with Relay OFF. */
static void sync_relay_membership(RyuLinkApp *app) {
    (void)ryuLinkLdnMitmIpcSetInternetRelayEnabled(false);
    if (!app->auth.vip_active) {
        (void)ryuLinkLdnMitmIpcSetRelayCredential(NULL);
        return;
    }
    if (!configure_virtual_ip(app)) return;
    (void)enable_relay(app);
}

static void set_network_mtu(RyuLinkApp *app) {
    (void)ryuLinkNetworkProfileSetCurrentMtu(1500);
    app->network_mtu_known = false;
    app->network_mtu_is_1500 = false;
    app->network_mtu_sync_pending = true;
    app->network_mtu_sync_after_ms = current_ms() + 5000;
}

static bool start_no_computer_relay(RyuLinkApp *app) {
    app->join_message[0] = '\0';
    (void)ryuLinkLdnMitmIpcSetInternetRelayEnabled(false);
    if (!require_relay_membership(app)) return false;
    app->vip_upsell_pending = false;
    if (!configure_virtual_ip(app)) {
        if (!app->join_message[0]) {
            snprintf(app->join_message, sizeof(app->join_message), "%s",
                     app->auth.message[0] ? app->auth.message : L("UNABLE TO GET VIRTUAL IP", "无法获取虚拟 IP"));
        }
        return false;
    }
    if (!enable_relay(app)) return false;
    snprintf(app->join_message, sizeof(app->join_message), "%s",
             L("RELAY READY - START YOUR GAME", "中继已就绪，启动游戏即可联机"));
    return true;
}

static bool join_api(RyuLinkApp *app, uint64_t room_id, const char *password, RyuLinkApiJoin *join) {
    return ryuLinkApiJoinRoom(&app->auth, room_id, app->device_id, password, join);
}

static bool join_room(RyuLinkApp *app) {
    RyuLinkApiJoin join;
    char password[129];
    const char *password_arg = NULL;
    password[0] = '\0';
    app->vip_upsell_pending = false;
    app->auth.needs_vip = false;
    app->join_message[0] = '\0';
    (void)ryuLinkLdnMitmIpcSetInternetRelayEnabled(false);
    if (app->room_detail.type[0] && !strcmp(app->room_detail.type, "VIP") && !app->auth.vip_active) {
        app->vip_upsell_pending = true;
        snprintf(app->join_message, sizeof(app->join_message), "%s",
                 L("VIP ZONE - MEMBERSHIP REQUIRED", "该房间为 VIP 专属区域，请开通会员后使用"));
        return false;
    }
    if (app->room_detail.password_required) {
        if (!prompt_password(password, sizeof(password))) {
            snprintf(app->join_message, sizeof(app->join_message), "%s", L("PASSWORD REQUIRED", "需要密码"));
            return false;
        }
        password_arg = password;
    }
    if (!configure_virtual_ip(app)) {
        memset(password, 0, sizeof(password));
        if (!app->join_message[0]) {
            snprintf(app->join_message, sizeof(app->join_message), "%s",
                     app->auth.message[0] ? app->auth.message : L("UNABLE TO GET VIRTUAL IP", "无法获取虚拟 IP"));
        }
        return false;
    }
    if (!join_api(app, app->room_detail.id, password_arg, &join)) {
        memset(password, 0, sizeof(password));
        if (app->auth.needs_vip) {
            app->vip_upsell_pending = true;
            snprintf(app->join_message, sizeof(app->join_message), "%s",
                     L("VIP ZONE - MEMBERSHIP REQUIRED", "该房间为 VIP 专属区域，请开通会员后使用"));
        } else {
            snprintf(app->join_message, sizeof(app->join_message), "%s",
                     app->auth.message[0] ? app->auth.message : L("UNABLE TO JOIN SPACE", "无法加入网络空间"));
        }
        return false;
    }
    memset(password, 0, sizeof(password));
    app->vip_upsell_pending = false;

    if (!enable_relay(app)) return false;

    remember_room_selection(app, &join);
    app->last_heartbeat_ms = 0;
    snprintf(app->join_message, sizeof(app->join_message), "%s",
             L("SPACE READY - RETURN HOME AND START GAME", "网络空间已就绪，返回主页后启动游戏"));
    return true;
}

/**
 * @brief Refresh a persisted selection once after login/startup.
 *
 * Re-joining revalidates the selected product room. The fixed ldn_mitm relay
 * profile remains independent from this App-only selection.
 */
static void refresh_selection_from_control_plane(RyuLinkApp *app) {
    RyuLinkApiJoin join;
    (void)ryuLinkLdnMitmIpcSetInternetRelayEnabled(false);
    if (!configure_virtual_ip(app) || !join_api(app, app->room_selection.room_id, NULL, &join) ||
        !enable_relay(app)) {
        (void)ryuLinkLdnMitmIpcSetInternetRelayEnabled(false);
        snprintf(app->join_message, sizeof(app->join_message), "%s",
                 L("UNABLE TO REFRESH SELECTED ROOM", "无法刷新已选房间，将使用默认服务器"));
        return;
    }
    remember_room_selection(app, &join);
    app->last_heartbeat_ms = 0;
}

static void clear_room_selection(RyuLinkApp *app) {
    ryuLinkRoomSelectionClear();
    memset(&app->room_selection, 0, sizeof(app->room_selection));
    app->last_heartbeat_ms = 0;
}

static void leave_room(RyuLinkApp *app) {
    if (!app->room_selection.active) return;
    if (!ryuLinkApiLeaveRoom(&app->auth, app->room_selection.room_id)) {
        snprintf(app->join_message, sizeof(app->join_message), "%s",
                 app->auth.message[0] ? app->auth.message : L("UNABLE TO LEAVE SPACE", "无法离开网络空间"));
        return;
    }
    clear_room_selection(app);
    snprintf(app->join_message, sizeof(app->join_message), "%s", L("SPACE SELECTION CLEARED", "已清除网络空间选择"));
}

static void begin_pending(RyuLinkApp *app, RyuLinkPendingAction action) {
    app->pending = action;
    ryuLinkLoadingBegin();
}

void ryuLinkAppRunPending(RyuLinkApp *app) {
    RyuLinkPendingAction action;
    if (!app || app->pending == RyuLinkPending_None) return;
    action = app->pending;
    app->pending = RyuLinkPending_None;
    switch (action) {
        case RyuLinkPending_LoginStart: ryuLinkAuthStart(&app->auth); break;
        case RyuLinkPending_AuthRestore:
            if (ryuLinkAuthRestore(&app->auth)) {
                sync_relay_membership(app);
                enter_lobby(app);
            }
            break;
        case RyuLinkPending_EnterLobby:
            sync_relay_membership(app);
            enter_lobby(app);
            break;
        case RyuLinkPending_RefreshRooms: refresh_rooms(app); break;
        case RyuLinkPending_GetRoom:
            if (app->room_page.count && app->selected_room < app->room_page.count &&
                ryuLinkApiGetRoom(&app->auth, app->room_page.rooms[app->selected_room].id, &app->room_detail)) {
                app->vip_upsell_pending = false;
                app->join_message[0] = '\0';
                app->page = RyuLinkPage_RoomDetail;
            }
            break;
        case RyuLinkPending_SetNetworkMtu: set_network_mtu(app); break;
        case RyuLinkPending_EnableNoComputerRelay: start_no_computer_relay(app); break;
        case RyuLinkPending_JoinRoom: join_room(app); break;
        case RyuLinkPending_LeaveRoom: leave_room(app); break;
        case RyuLinkPending_SetPreferredNode:
            if (app->node_count) ryuLinkApiSetPreferredNode(&app->auth, app->nodes[app->selected_node].id);
            break;
        case RyuLinkPending_Logout:
            (void)ryuLinkLdnMitmIpcSetInternetRelayEnabled(false);
            if (app->room_selection.active)
                (void)ryuLinkApiLeaveRoom(&app->auth, app->room_selection.room_id);
            clear_room_selection(app);
            ryuLinkAuthCancel(&app->auth);
            app->page = app->pending_target;
            break;
        default: break;
    }
    ryuLinkLoadingEnd();
}

void ryuLinkAppHandleInput(RyuLinkApp *app, u64 buttons) {
    if (app->pending != RyuLinkPending_None) return;
    if (app->page == RyuLinkPage_Splash) {
        if ((buttons & HidNpadButton_A) || current_ms() - app->splash_started_ms > 1200)
            app->page = RyuLinkPage_Login;
        return;
    }
    if (app->page == RyuLinkPage_Login) {
        if (app->auth.state == RyuLinkAuth_Idle && ryuLinkAuthHasPersistedSession()) { begin_pending(app, RyuLinkPending_AuthRestore); return; }
        if (app->auth.state == RyuLinkAuth_AwaitingBrowser) {
            ryuLinkAuthUpdate(&app->auth);
            if (app->auth.state == RyuLinkAuth_Authenticated) { begin_pending(app, RyuLinkPending_EnterLobby); return; }
        }
        if (buttons & HidNpadButton_B) { ryuLinkAuthCancel(&app->auth); app->page = RyuLinkPage_Splash; }
        else if (buttons & HidNpadButton_A) begin_pending(app, RyuLinkPending_LoginStart);
        return;
    }
    if (app->auth.state != RyuLinkAuth_Authenticated) { app->page = RyuLinkPage_Login; return; }
    /* Bottom navigation must work on every authenticated page (Home,
       Profile, Settings, and RoomDetail per acceptance). Handle L/Y/R
       before the page-specific branches so their early returns cannot
       swallow navigation; page-specific actions keep their priority. */
    if (buttons & HidNpadButton_L) { app->page = RyuLinkPage_Home; return; }
    if (buttons & HidNpadButton_Y) { app->page = RyuLinkPage_Profile; return; }
    if (buttons & HidNpadButton_R) { app->page = RyuLinkPage_Settings; return; }
    if (app->page == RyuLinkPage_RoomDetail) {
        if (buttons & HidNpadButton_B) {
            app->join_message[0] = '\0';
            app->vip_upsell_pending = false;
            app->page = RyuLinkPage_Home;
        } else if ((buttons & HidNpadButton_X) && app->room_selection.active) {
            begin_pending(app, RyuLinkPending_LeaveRoom);
        } else if (buttons & HidNpadButton_A) {
            if (app->vip_upsell_pending) {
                app->vip_upsell_pending = false;
                app->profile_action = 0;
                app->page = RyuLinkPage_Profile;
            } else if (!app->room_selection.active || app->room_selection.room_id != app->room_detail.id) {
                begin_pending(app, RyuLinkPending_JoinRoom);
            }
        }
        return;
    }
    if (app->page == RyuLinkPage_Profile) {
        if (buttons & (HidNpadButton_Up | HidNpadButton_Down)) app->profile_action = (app->profile_action + 1) % 2;
        if (app->profile_action == 0 && app->node_count && (buttons & HidNpadButton_Left)) app->selected_node = (app->selected_node + app->node_count - 1) % app->node_count;
        if (app->profile_action == 0 && app->node_count && (buttons & HidNpadButton_Right)) app->selected_node = (app->selected_node + 1) % app->node_count;
        if ((buttons & HidNpadButton_A) && app->profile_action == 0 && app->node_count) begin_pending(app, RyuLinkPending_SetPreferredNode);
        if ((buttons & HidNpadButton_A) && app->profile_action == 1) { app->pending_target = RyuLinkPage_Login; begin_pending(app, RyuLinkPending_Logout); }
        if (buttons & HidNpadButton_B) app->page = RyuLinkPage_Home;
        return;
    }
    if (app->page == RyuLinkPage_Settings) {
        if (buttons & HidNpadButton_B) app->page = RyuLinkPage_Home;
        return;
    }
    if (app->page != RyuLinkPage_Home) return;
    if ((buttons & HidNpadButton_X) && app->relay_mode == RyuLinkRelayMode_NoComputerBeta) {
        begin_pending(app, RyuLinkPending_SetNetworkMtu);
        return;
    }
    if ((buttons & HidNpadButton_A) && app->relay_mode == RyuLinkRelayMode_NoComputerBeta) {
        begin_pending(app, RyuLinkPending_EnableNoComputerRelay);
        return;
    }
    if (buttons & (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right))
        app->relay_mode = app->relay_mode == RyuLinkRelayMode_NoComputerBeta
                              ? RyuLinkRelayMode_Computer : RyuLinkRelayMode_NoComputerBeta;
}

void ryuLinkAppHandleTouch(RyuLinkApp *app, uint32_t x, uint32_t y) {
    if (app->pending != RyuLinkPending_None) return;
    if (app->page == RyuLinkPage_Splash) {
        app->page = RyuLinkPage_Login;
        return;
    }
    if (app->page == RyuLinkPage_Login) {
        if (app->auth.state == RyuLinkAuth_AwaitingBrowser && y >= 500) {
            ryuLinkAuthCancel(&app->auth);
            app->page = RyuLinkPage_Splash;
        } else if (app->auth.state != RyuLinkAuth_AwaitingBrowser) {
            begin_pending(app, RyuLinkPending_LoginStart);
        }
        return;
    }
    if (app->auth.state != RyuLinkAuth_Authenticated) {
        app->page = RyuLinkPage_Login;
        return;
    }
    if (y >= 640) {
        if (x >= 360 && x < 570) app->page = RyuLinkPage_Home;
        else if (x >= 570 && x < 760) app->page = RyuLinkPage_Profile;
        else if (x >= 760 && x < 970) app->page = RyuLinkPage_Settings;
        return;
    }
    if (app->page == RyuLinkPage_RoomDetail) {
        if (app->vip_upsell_pending && y >= 500 && y < 560) {
            app->vip_upsell_pending = false;
            app->profile_action = 0;
            app->page = RyuLinkPage_Profile;
        } else if (y >= 450 && y < 520 && !app->vip_upsell_pending &&
            (!app->room_selection.active || app->room_selection.room_id != app->room_detail.id))
            begin_pending(app, RyuLinkPending_JoinRoom);
        else if (y >= 500) {
            app->join_message[0] = '\0';
            app->vip_upsell_pending = false;
            app->page = RyuLinkPage_Home;
        }
        return;
    }
    if (app->page == RyuLinkPage_Profile) {
        if (y >= 430 && y < 510) {
            app->pending_target = RyuLinkPage_Login;
            begin_pending(app, RyuLinkPending_Logout);
        } else if (y >= 370 && y < 430 && app->node_count) {
            begin_pending(app, RyuLinkPending_SetPreferredNode);
        }
        return;
    }
    if (app->page != RyuLinkPage_Home) return;
    if (x >= 80 && x < 300 && y >= 240 && y < 400)
        app->relay_mode = y < 320 ? RyuLinkRelayMode_NoComputerBeta : RyuLinkRelayMode_Computer;
    else if (app->relay_mode == RyuLinkRelayMode_NoComputerBeta &&
             x >= 390 && x < 570 && y >= 438 && y < 510) {
        begin_pending(app, RyuLinkPending_SetNetworkMtu);
    } else if (app->relay_mode == RyuLinkRelayMode_NoComputerBeta &&
             x >= 590 && x < 930 && y >= 438 && y < 510) {
        begin_pending(app, RyuLinkPending_EnableNoComputerRelay);
    }
}

static void draw_loading_overlay(void) {
    /* Larger square backdrop with the loading label centered inside. */
    ryuLinkUiRoundedPanel(490, 210, 300, 300, 20, ColorPanelMuted);
    ryuLinkUiCenteredText(349, 3, ColorPrimary, L("Loading...", "加载中"));
}

void ryuLinkAppDraw(const RyuLinkApp *app) {
    ryuLinkUiFill(ColorBackground);
    switch (app->page) { case RyuLinkPage_Splash: draw_splash(); break; case RyuLinkPage_Login: draw_login(app); break; case RyuLinkPage_Home: draw_home(app); break; case RyuLinkPage_RoomDetail: draw_room_detail(app); break; case RyuLinkPage_Profile: draw_profile(app); break; case RyuLinkPage_Settings: draw_settings(); break; }
    if (ryuLinkLoadingActive()) draw_loading_overlay();
}
