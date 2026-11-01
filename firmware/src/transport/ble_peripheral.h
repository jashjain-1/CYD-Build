#ifndef DASHCUE_TRANSPORT_BLE_PERIPHERAL_H
#define DASHCUE_TRANSPORT_BLE_PERIPHERAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Stable GATT service and characteristic identifiers shared with the phone app.
#define DASHCUE_SERVICE_UUID           "7d1a0001-5c3e-4b8a-9f21-3a6c8e0d4b10"
#define DASHCUE_RX_CHAR_UUID           "7d1a0002-5c3e-4b8a-9f21-3a6c8e0d4b10"
#define DASHCUE_TX_CHAR_UUID           "7d1a0003-5c3e-4b8a-9f21-3a6c8e0d4b10"

// Legacy alternate identifiers retained for source compatibility; not advertised.
#define DASHCUE_SERVICE_UUID_ALT       "7d1a0001-2e98-4c3e-a89f-8647c4078a6c"
#define DASHCUE_RX_CHAR_UUID_ALT       "7d1a0002-2e98-4c3e-a89f-8647c4078a6c"
#define DASHCUE_TX_CHAR_UUID_ALT       "7d1a0003-2e98-4c3e-a89f-8647c4078a6c"

/**
 * Initialize the Bluedroid peripheral, create the DashCue GATT service with
 * unauthenticated RX/TX characteristics, and start advertising.
 *
 * There is no BLE pairing step: the HUD accepts any central that connects and
 * the phone app talks to it directly. Authentication/pairing on the BLE link was
 * removed by product decision (see docs/security/threat-model.md, "BLE link").
 */
bool ble_peripheral_init(void);

/**
 * Remove stored Bluetooth peer identity and disconnect the active link.
 * This clears bond state used by Classic Bluetooth A2DP; BLE GATT itself does
 * not require a bond.
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
 * Advertise the DashCue GATT service whenever no central is connected.
 */
void ble_peripheral_update_advertising(void);

/**
 * MAC & device identity helpers.
 */
void ble_peripheral_get_mac(uint8_t mac[6]);
void ble_peripheral_get_device_id(char *out_id, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_TRANSPORT_BLE_PERIPHERAL_H
