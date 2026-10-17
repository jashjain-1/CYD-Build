#ifndef DASHCUE_TRANSPORT_BLE_CONFIG_H
#define DASHCUE_TRANSPORT_BLE_CONFIG_H

#include <stdint.h>
#include "dashcue_config.h"
#include "dashcue_version.h"

// Authoritative frozen UUIDs (protocol/uuids.md and tests/e2e/test_tier1_features.py)
#define DASHCUE_SERVICE_UUID           "7d1a0001-5c3e-4b8a-9f21-3a6c8e0d4b10"
#define DASHCUE_RX_CHAR_UUID           "7d1a0002-5c3e-4b8a-9f21-3a6c8e0d4b10"
#define DASHCUE_TX_CHAR_UUID           "7d1a0003-5c3e-4b8a-9f21-3a6c8e0d4b10"

// Alternate UUID aliases (dispatched in PROJECT.md)
#define DASHCUE_SERVICE_UUID_ALT       "7d1a0001-2e98-4c3e-a89f-8647c4078a6c"
#define DASHCUE_RX_CHAR_UUID_ALT       "7d1a0002-2e98-4c3e-a89f-8647c4078a6c"
#define DASHCUE_TX_CHAR_UUID_ALT       "7d1a0003-2e98-4c3e-a89f-8647c4078a6c"

// Standard Bluetooth SIG 16-bit Service & Characteristic UUIDs
#define BLE_UUID_DIS_SERVICE           0x180A
#define BLE_UUID_DIS_MANUFACTURER_NAME 0x2A29
#define BLE_UUID_DIS_MODEL_NUMBER      0x2A24
#define BLE_UUID_DIS_SERIAL_NUMBER     0x2A25
#define BLE_UUID_DIS_FW_REVISION       0x2A26
#define BLE_UUID_DIS_HW_REVISION       0x2A27

// Device Information Strings
#define DASHCUE_BLE_MANUFACTURER_NAME  "DashCue"
#define DASHCUE_BLE_MODEL_NUMBER       "DashCue HUD"
#define DASHCUE_BLE_FW_VERSION         "1.0.0"
#define DASHCUE_BLE_HW_REVISION        DASHCUE_HW_REVISION

// Pairing & Advertising parameters
#define DASHCUE_BLE_DEVICE_NAME        "DashCue-HUD"
#define DASHCUE_PAIRING_WINDOW_MS      120000 // 120 seconds
#define DASHCUE_PREFERRED_MTU          247
#define DASHCUE_MIN_CONN_INTERVAL      12     // 15 ms (12 * 1.25ms)
#define DASHCUE_MAX_CONN_INTERVAL      24     // 30 ms (24 * 1.25ms)
#define DASHCUE_SLAVE_LATENCY          0
#define DASHCUE_SUPERVISION_TIMEOUT    400    // 4000 ms (400 * 10ms)

#endif // DASHCUE_TRANSPORT_BLE_CONFIG_H
