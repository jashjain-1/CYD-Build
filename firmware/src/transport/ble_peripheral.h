#ifndef DASHCUE_TRANSPORT_BLE_PERIPHERAL_H
#define DASHCUE_TRANSPORT_BLE_PERIPHERAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Frozen UUID definitions per protocol/uuids.md
#define DASHCUE_SERVICE_UUID           "7d1a0001-5c3e-4b8a-9f21-3a6c8e0d4b10"
#define DASHCUE_RX_CHAR_UUID           "7d1a0002-5c3e-4b8a-9f21-3a6c8e0d4b10"
#define DASHCUE_TX_CHAR_UUID           "7d1a0003-5c3e-4b8a-9f21-3a6c8e0d4b10"

// Alternate UUID definitions
#define DASHCUE_SERVICE_UUID_ALT       "7d1a0001-2e98-4c3e-a89f-8647c4078a6c"
#define DASHCUE_RX_CHAR_UUID_ALT       "7d1a0002-2e98-4c3e-a89f-8647c4078a6c"
#define DASHCUE_TX_CHAR_UUID_ALT       "7d1a0003-2e98-4c3e-a89f-8647c4078a6c"

#define PAIRING_WINDOW_TIMEOUT_DEFAULT_S 120u

/**
 * Initialize NimBLE peripheral, configure GAP security (LE SC, MITM, DisplayOnly),
 * create GATT service and encrypted/authenticated RX/TX characteristics,
 * populate DIS (0x180A), and configure initial advertising.
 */
bool ble_peripheral_init(void);

/**
 * Enter 120 s pairing window (triggered explicitly via UI: More -> Pair Phone).
 * Advertises as General Discoverable with name DashCue-HUD.
 */
void ble_peripheral_start_pairing_window(uint32_t timeout_s);

/**
 * Cancel / stop pairing window.
 */
void ble_peripheral_cancel_pairing_window(void);

/**
 * Forget bonded phone: erases bond from NVS, clears accept-list, resets state.
 */
void ble_peripheral_forget_phone(void);

/**
 * Send notification chunk over TX characteristic.
 */
bool ble_peripheral_send_notify(const uint8_t *data, size_t length);

/**
 * Query current link status and parameters.
 */
bool ble_peripheral_is_connected(void);
uint16_t ble_peripheral_get_mtu(void);
uint16_t ble_peripheral_get_conn_handle(void);

/**
 * Update advertising state according to bonding and pairing mode.
 */
void ble_peripheral_update_advertising(void);

/**
 * Periodic tick for pairing window countdown.
 */
void ble_peripheral_tick(uint32_t now_ms);

/**
 * MAC & device identity helpers.
 */
void ble_peripheral_get_mac(uint8_t mac[6]);
void ble_peripheral_get_device_id(char *out_id, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_TRANSPORT_BLE_PERIPHERAL_H
