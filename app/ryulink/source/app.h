#pragma once

#include "ui.h"
#include "auth.h"

enum {
    RyuLinkRoomTypeCount = 1,
    RyuLinkRoomCount = RyuLinkApiRoomLimit,
};

typedef enum {
    RyuLinkPage_Splash,
    RyuLinkPage_Login,
    RyuLinkPage_Home,
    RyuLinkPage_RoomDetail,
    RyuLinkPage_Profile,
    RyuLinkPage_Settings,
} RyuLinkPage;

typedef enum {
    RyuLinkLogin_Ready,
    RyuLinkLogin_WaitingForAuthorization,
} RyuLinkLoginState;

typedef enum {
    RyuLinkPending_None = 0,
    RyuLinkPending_LoginStart,
    RyuLinkPending_AuthRestore,
    RyuLinkPending_EnterLobby,
    RyuLinkPending_RefreshRooms,
    RyuLinkPending_GetRoom,
    RyuLinkPending_EnableNoComputerRelay,
    RyuLinkPending_JoinRoom,
    RyuLinkPending_LeaveRoom,
    RyuLinkPending_SetPreferredNode,
    RyuLinkPending_Logout,
} RyuLinkPendingAction;

typedef enum {
    RyuLinkSpace_Public = 0,
    RyuLinkSpace_Vip = 1,
    RyuLinkSpace_Private = 2,
} RyuLinkSpaceKind;

typedef enum {
    RyuLinkRelayMode_NoComputerBeta,
    RyuLinkRelayMode_Computer,
} RyuLinkRelayMode;

typedef struct {
    bool active;
    uint64_t room_id;
    uint64_t revision;
    char room_name[101];
} RyuLinkRoomSelection;

typedef struct {
    RyuLinkPage page;
    RyuLinkLoginState login_state;
    int selected_room;
    /** True when focus is on the left roomType list; false when on the room list. */
    bool type_list_focused;
    RyuLinkSpaceKind space_kind;
    RyuLinkRelayMode relay_mode;
    u8 profile_action;
    u8 selected_node;
    u8 node_count;
    u64 splash_started_ms;
    RyuLinkPendingAction pending;
    RyuLinkPage pending_target;
    RyuLinkAuthSession auth;
    RyuLinkApiRoomPage room_page;
    RyuLinkApiRoom room_detail;
    RyuLinkApiNode nodes[RyuLinkApiNodeLimit];
    char device_id[33];
    char join_message[128];
    /** True after VIP_REQUIRED on join — A opens membership (profile). */
    bool vip_upsell_pending;
    RyuLinkRoomSelection room_selection;
    /** True once the persisted selection has been refreshed/applied this boot. */
    bool selection_applied_this_boot;
    u64 last_heartbeat_ms;
} RyuLinkApp;

void ryuLinkAppInitialize(RyuLinkApp *app);
/** Executes the network action deferred by the previous input frame.
 *  Runs with the loading overlay already presented; clears loading on exit. */
void ryuLinkAppRunPending(RyuLinkApp *app);
/** Foreground tick: room heartbeat every ~90s while a space is selected. */
void ryuLinkAppUpdate(RyuLinkApp *app);
void ryuLinkAppHandleInput(RyuLinkApp *app, u64 buttons);
void ryuLinkAppHandleTouch(RyuLinkApp *app, uint32_t x, uint32_t y);
void ryuLinkAppDraw(const RyuLinkApp *app);
