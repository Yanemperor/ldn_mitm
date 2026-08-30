#pragma once

#include <switch.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum { RyuLinkAuth_Idle, RyuLinkAuth_AwaitingBrowser, RyuLinkAuth_Authenticated, RyuLinkAuth_Error } RyuLinkAuthState;

typedef struct {
    RyuLinkAuthState state;
    char user_code[128];
    char verification_uri[512];
    char verification_uri_complete[4097];
    char display_name[101];
    char player_code[33];
    char message[128];
    uint64_t next_poll_ms;
    uint64_t expires_at_ms;
    uint32_t poll_interval_seconds;
    uint32_t network_retry_seconds;
    bool test_access_enabled;
    bool account_disabled;
    bool vip_active;
    /** Set by join when server returns 40302 VIP_REQUIRED. */
    bool needs_vip;
    /** Set by the virtual-IP endpoint when the cached server device must be restored. */
    bool device_not_registered;
    char private_room_name[101];
} RyuLinkAuthSession;

enum { RyuLinkApiRoomLimit = 1, RyuLinkApiNodeLimit = 8 };

typedef struct {
    uint64_t id;
    char display_game_id[65];
    char type[16];
    char name[101];
    char owner_display_name[101];
    uint8_t display_room_number;
    uint16_t online_player_count;
    uint16_t capacity;
    bool password_required;
    bool is_owner;
    char runtime_status[16];
} RyuLinkApiRoom;

typedef struct {
    RyuLinkApiRoom rooms[RyuLinkApiRoomLimit];
    uint8_t count;
    uint32_t total;
} RyuLinkApiRoomPage;

typedef struct {
    char id[65];
    char name[101];
    bool available;
    bool preferred;
} RyuLinkApiNode;

typedef struct {
    uint64_t room_id;
    char room_name[101];
} RyuLinkApiJoin;

typedef struct {
    char evidence_id[65];
    uint32_t stored_bytes;
} RyuLinkApiEvidenceUpload;

bool ryuLinkAuthInitialize(void);
void ryuLinkAuthExit(void);
bool ryuLinkAuthHasPersistedSession(void);
bool ryuLinkAuthRestore(RyuLinkAuthSession *session);
void ryuLinkAuthStart(RyuLinkAuthSession *session);
void ryuLinkAuthUpdate(RyuLinkAuthSession *session);
void ryuLinkAuthCancel(RyuLinkAuthSession *session);

/* Unified request loading state. Every HTTP request counts toward the
   global loading counter; the App draws a loading overlay while the
   counter is non-zero. */
void ryuLinkLoadingBegin(void);
void ryuLinkLoadingEnd(void);
bool ryuLinkLoadingActive(void);
bool ryuLinkApiListRooms(RyuLinkAuthSession *session, const char *type,
                         const char *display_game_id, RyuLinkApiRoomPage *page);
bool ryuLinkApiGetRoom(RyuLinkAuthSession *session, uint64_t room_id, RyuLinkApiRoom *room);
bool ryuLinkApiListNodes(RyuLinkAuthSession *session, RyuLinkApiNode *nodes, uint8_t *count);
bool ryuLinkApiSetPreferredNode(RyuLinkAuthSession *session, const char *node_id);
/** Gets the persisted server device UUID, registering the persistent bootstrap
    identity when missing or when force_register is true. */
bool ryuLinkApiEnsureServerDevice(RyuLinkAuthSession *session, bool force_register,
                                  char out_device_id[37]);
/** Gets this device's permanent virtual IP. */
bool ryuLinkApiGetVirtualIp(RyuLinkAuthSession *session, const char *server_device_id,
                            char out_virtual_ip[16]);
/** password may be NULL/empty for PUBLIC and VIP zones; required for PRIVATE (私服). */
bool ryuLinkApiJoinRoom(RyuLinkAuthSession *session, uint64_t room_id, const char *device_id,
                        const char *password, RyuLinkApiJoin *join);
bool ryuLinkApiLeaveRoom(RyuLinkAuthSession *session, uint64_t room_id);
/** Maintain ACTIVE membership; fails soft (returns false) if no seat — App keeps local selection. */
bool ryuLinkApiHeartbeatRoom(RyuLinkAuthSession *session, uint64_t room_id);
/** Upload a pre-redacted, bounded diagnostic evidence bundle over HTTPS. */
bool ryuLinkApiUploadEvidence(RyuLinkAuthSession *session, const char *path,
                              RyuLinkApiEvidenceUpload *upload);
