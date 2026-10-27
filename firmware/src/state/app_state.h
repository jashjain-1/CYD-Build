#ifndef DASHCUE_STATE_APP_STATE_H
#define DASHCUE_STATE_APP_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ipc_messages.h"
#include "ipc_queues.h"
#include "dashcue_config.h"
#include "protocol/generated/dashcue.pb.h"

#ifndef DASHCUE_NATIVE_TEST
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#else
typedef void* SemaphoreHandle_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// BLE internal RX queue handle
extern QueueHandle_t g_ble_rx_queue;

/**
 * Initialize system state, FreeRTOS IPC queues, and sync primitives.
 */
bool app_state_init(void);

/**
 * Mutex locking for thread-safe state access across Core 0 and Core 1.
 */
void app_state_lock(void);
void app_state_unlock(void);

// Peripheral state machine
HudPeripheralState app_state_get_peripheral_state(void);
void app_state_set_peripheral_state(HudPeripheralState state);

// Connection status & MTU
bool app_state_is_connected(void);
void app_state_set_connected(bool connected, uint16_t handle, uint16_t mtu);
uint16_t app_state_get_mtu(void);
void app_state_set_mtu(uint16_t mtu);
void app_state_set_rssi(int32_t rssi_dbm);

// Pairing & Security
void app_state_start_pairing_window(uint32_t timeout_s);
void app_state_stop_pairing_window(void);
bool app_state_is_pairing_window_active(void);
uint32_t app_state_get_pairing_remaining_s(void);
void app_state_tick_pairing_window(void);

void app_state_set_passkey(uint32_t passkey);
uint32_t app_state_get_passkey(void);

bool app_state_has_bonded_phone(void);
/** Snapshot of the bonded phone's BD_ADDR, or NULL when unbonded. The snapshot
 *  is a function-local static, so it is stable until the next call. */
const uint8_t *app_state_get_bonded_addr(void);
void app_state_set_bonded(bool bonded, const uint8_t *mac);

/**
 * Rejects bond-list entries that cannot be a real classic-BT phone.
 *
 * A bogus address was observed inside the GAP bond device list on hardware
 * (A5:A5:A5:A5:A5:A5, a test-filler pattern) and was then promoted into the
 * NVS last-bonded-peer by the restore path, sending reconnects at an address
 * no phone ever had. Real public addresses are never all-zero, all-0xFF, or a
 * block of one repeated octet; the check is cheap and cannot reject a phone
 * whose OUI happens to have identical bytes across all six octets (no
 * manufacturer is allocated such an OUI).
 */
bool app_state_is_valid_bt_addr(const uint8_t *bda);
void app_state_clear_bond(void);

// Telemetry & Protocol Handlers
void app_state_on_heartbeat_rx(const dashcue_v1_Heartbeat *hb, uint32_t now_ms);
void app_state_on_media_state(const dashcue_v1_MediaState *ms, uint32_t now_ms);
void app_state_on_nav_state(const dashcue_v1_NavState *ns, uint32_t now_ms);
void app_state_on_settings_set(const dashcue_v1_SettingsData *s);
void app_state_get_settings(dashcue_v1_SettingsData *out_settings);

// Freshness checks & Driving Lock
void app_state_check_freshness(uint32_t now_ms);

/**
 * Drops every phone-authored snapshot (media, nav, freshness, speed) after a
 * link teardown or an explicit end-of-route.
 *
 * This is the single anti-ghost primitive: without it the HUD keeps painting
 * the last track and the last turn cue from a session that no longer exists.
 * The snapshots are zeroed and their status set to the explicit "nothing here"
 * values (PB_NO_SESSION / NAV_NONE) rather than left at whatever arrived last.
 */
void app_state_reset_session(void);
bool app_state_is_driving_locked(void);
bool app_state_is_phone_stale(void);
bool app_state_is_nav_stale(void);
bool app_state_is_media_stale(void);
int32_t app_state_get_speed_kmh_x10(void);

// SafeMode (TC-HW-11)
void app_state_record_boot(uint32_t reset_reason, uint32_t now_s);
void app_state_mark_stable_run(void);
bool app_state_is_safe_mode(void);
uint32_t app_state_get_crash_count(void);

// Snapshots & Diagnostics
void app_state_get_media_snapshot(dashcue_v1_MediaState *out_ms);
void app_state_get_nav_snapshot(dashcue_v1_NavState *out_ns);
void app_state_get_device_status(dashcue_v1_DeviceStatus *out_status, uint32_t now_s);

// Error and drop counters
void app_state_inc_crc_error(void);
void app_state_inc_queue_drop(void);
void app_state_inc_frag_timeout(void);
void app_state_inc_dup_command(void);

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_STATE_APP_STATE_H
