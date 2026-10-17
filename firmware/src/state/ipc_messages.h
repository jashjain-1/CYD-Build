#ifndef DASHCUE_STATE_IPC_MESSAGES_H
#define DASHCUE_STATE_IPC_MESSAGES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "protocol/generated/dashcue.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROTO_TO_UI_QUEUE_LEN  16u
#define UI_TO_PROTO_QUEUE_LEN  16u
#define BLE_RX_QUEUE_LEN       32u

/**
 * HUD peripheral states (§9.6):
 * Booting -> Advertising -> (AdvertisingPairing) -> ConnectedUnsynced -> Ready -> Stale -> Advertising
 */
typedef enum {
    HUD_STATE_BOOTING = 0,
    HUD_STATE_ADVERTISING = 1,
    HUD_STATE_ADVERTISING_PAIRING = 2,
    HUD_STATE_CONNECTED_UNSYNCED = 3,
    HUD_STATE_READY = 4,
    HUD_STATE_STALE = 5,
    HUD_STATE_SAFE_MODE = 6
} HudPeripheralState;

/**
 * Events sent from proto_task (Core 0) to ui_task (Core 1).
 */
typedef enum {
    IPC_EVT_NONE = 0,
    IPC_EVT_BLE_STATE,
    IPC_EVT_PASSKEY,
    IPC_EVT_PASSKEY_DISMISSED,
    IPC_EVT_MEDIA_STATE,
    IPC_EVT_NAV_STATE,
    IPC_EVT_ROUTE_OVERVIEW,
    IPC_EVT_SETTINGS_STATE,
    IPC_EVT_HEARTBEAT,
    IPC_EVT_CMD_RESULT,
    IPC_EVT_ERROR,
    IPC_EVT_DEVICE_STATUS,
    IPC_EVT_DRIVING_LOCK,
    IPC_EVT_FRESHNESS,
    IPC_EVT_ARTWORK_READY
} IpcEventType;

typedef struct {
    HudPeripheralState state;
    bool is_connected;
    uint16_t mtu;
} IpcBleStateEvt;

typedef struct {
    uint32_t passkey;
    uint32_t timeout_s;
} IpcPasskeyEvt;

typedef struct {
    bool locked;
    int32_t speed_kmh_x10;
} IpcDrivingLockEvt;

typedef struct {
    bool nav_stale;
    bool media_stale;
    bool phone_stale;
} IpcFreshnessEvt;

/**
 * Discriminated union for events queued from proto_task to ui_task.
 */
typedef struct {
    IpcEventType type;
    union {
        struct {
            bool connected;
            bool authenticated;
            uint16_t mtu;
            int32_t rssi;
            char peer_addr[32];
        } link;
        struct {
            uint32_t passkey;
            uint32_t remaining_seconds;
        } pairing;
        IpcBleStateEvt      ble_state;
        IpcPasskeyEvt       passkey;
        IpcDrivingLockEvt   driving_lock;
        IpcFreshnessEvt     freshness;
        // Media/nav/route events are notifications: app_state owns the latest
        // snapshots. Never replicate a 120-point route in every queue slot.
        dashcue_v1_SettingsState  settings_state;
        dashcue_v1_Heartbeat      heartbeat;
        dashcue_v1_CmdResult      cmd_result;
        dashcue_v1_ErrorMsg       error;
        dashcue_v1_DeviceStatus   device_status;
        struct {
            uint32_t generation;
        } artwork;
    } data;
} ProtoToUiMessage;

#ifdef __cplusplus
static_assert(sizeof(ProtoToUiMessage) <= 256,
              "UI event queue must fit within 4 KiB at 16 entries");
#endif

#define PROTO_EVENT_BLE_LINK_STATE          IPC_EVT_BLE_STATE
#define PROTO_EVENT_PAIRING_PROMPT_PASSKEY  IPC_EVT_PASSKEY
#define PROTO_EVENT_PAIRING_SUCCESS         IPC_EVT_BLE_STATE

/**
 * Commands sent from ui_task (Core 1) to proto_task (Core 0).
 */
typedef enum {
    IPC_CMD_NONE = 0,
    IPC_CMD_MEDIA,
    IPC_CMD_NAV,
    IPC_CMD_SETTINGS_SET,
    IPC_CMD_SNAPSHOT_REQ,
    IPC_CMD_PAIR_PHONE,
    IPC_CMD_CANCEL_PAIRING,
    IPC_CMD_FORGET_PHONE,
    IPC_CMD_DIAG_REQ
} IpcCommandType;

#define UI_CMD_START_PAIRING   IPC_CMD_PAIR_PHONE
#define UI_CMD_CANCEL_PAIRING  IPC_CMD_CANCEL_PAIRING
#define UI_CMD_UNPAIR_PHONE    IPC_CMD_FORGET_PHONE
#define UI_CMD_MEDIA           IPC_CMD_MEDIA
#define UI_CMD_NAV             IPC_CMD_NAV
#define UI_CMD_SETTINGS_SET    IPC_CMD_SETTINGS_SET
#define UI_CMD_SNAPSHOT_REQ    IPC_CMD_SNAPSHOT_REQ

typedef struct {
    IpcCommandType type;
    union {
        dashcue_v1_MediaCmd        media;
        dashcue_v1_MediaCmd        media_cmd;
        dashcue_v1_NavCmd          nav;
        dashcue_v1_NavCmd          nav_cmd;
        dashcue_v1_SettingsSet     settings;
        dashcue_v1_SettingsSet     settings_set;
        dashcue_v1_SnapshotRequest snapshot;
        dashcue_v1_SnapshotRequest snapshot_req;
        dashcue_v1_DiagReq         diag_req;
        struct {
            uint32_t timeout_s;
        } pair_phone;
    } data;
} UiToProtoMessage;

/**
 * Raw fragment packet received from BLE RX callback to feed into proto_task.
 */
typedef struct {
    uint16_t len;
    uint8_t  data[256];
} BleRxPacket;

typedef ProtoToUiMessage ProtoToUiMsg;
typedef UiToProtoMessage UiToProtoMsg;

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_STATE_IPC_MESSAGES_H
