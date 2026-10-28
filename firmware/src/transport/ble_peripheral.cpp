#ifndef DASHCUE_NATIVE_TEST

// DashCue HUD GATT peripheral.
//
// GATE-05 (NimBLE) FAILED on the pinned framework: the prebuilt Arduino-ESP32 SDK
// ships Bluedroid only - libbt.a contains zero NimBLE objects and
// sdkconfig.h has CONFIG_BT_BLUEDROID_ENABLED=1 with no CONFIG_BT_NIMBLE_ENABLED -
// so NimBLEDevice::init() blocked forever and the HUD never advertised. DEC-05's
// documented fallback ("Bluedroid only if GATE-05 fails") is therefore taken, and
// recorded in docs/DECISIONS.md (D-10) and docs/VERIFICATION.md (GATE-05).
//
// The public API in ble_peripheral.h is unchanged, so proto_task.cpp, the UI and
// the state machine do not know which host is underneath.
#include "ble_peripheral.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_bt.h>
#include <esp_gap_ble_api.h>
#include <esp_gatt_defs.h>
#include <esp_mac.h>

#include "dashcue_config.h"
#include "dashcue_version.h"
#include "state/app_state.h"
#include "state/ipc_queues.h"
#include "ui/artwork_store.h"

#if defined(DASHCUE_ENABLE_A2DP)
#include <esp_gap_bt_api.h>
extern "C" void dashcue_a2dp_connect_peer(const uint8_t *bda);
extern "C" void dashcue_a2dp_forget_peer(void);
#endif

namespace {

static BLEServer*          s_pServer = nullptr;
static BLECharacteristic*  s_pRxChar = nullptr;
static BLECharacteristic*  s_pTxChar = nullptr;

static volatile uint16_t s_conn_handle = 0xFFFF; // 0xFFFF = none, same convention as BLE_HS_CONN_HANDLE_NONE
static volatile bool     s_is_connected = false;
static volatile uint16_t s_negotiated_mtu = 23;

static volatile uint32_t s_pairing_start_ms = 0;
static volatile uint32_t s_pairing_duration_ms = 0;
static volatile bool     s_pairing_active = false;
static volatile bool     s_peripheral_inited = false;

static char s_device_id_str[32] = {0};

void format_mac_strings(void) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);

    snprintf(s_device_id_str, sizeof(s_device_id_str), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void notify_ui_ble_state(HudPeripheralState state, bool connected, uint16_t mtu) {
    ProtoToUiMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_EVT_BLE_STATE;
    msg.data.ble_state.state = state;
    msg.data.ble_state.is_connected = connected;
    msg.data.ble_state.mtu = mtu;
    ipc_send_proto_to_ui(&msg, 0);
}

bool is_bonded_peer(const esp_bd_addr_t bda) {
    int dev_num = esp_ble_get_bond_device_num();
    if (dev_num <= 0) {
        return false;
    }
    esp_ble_bond_dev_t* dev_list =
        (esp_ble_bond_dev_t*)malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
    if (dev_list == nullptr) {
        return false;
    }
    int query_num = dev_num;
    esp_ble_get_bond_device_list(&query_num, dev_list);

    bool bonded = false;
    for (int i = 0; i < query_num; ++i) {
        if (memcmp(dev_list[i].bd_addr, bda, sizeof(esp_bd_addr_t)) == 0) {
            bonded = true;
            break;
        }
    }
    free(dev_list);
    return bonded;
}

// GAP handler: the passkey and bond lifecycle arrive here. Bluedroid's own handler
// is replaced by this one via BLEDevice::setCustomGapHandler(), so the events this
// project cares about are handled here and the rest are forwarded to Bluedroid's.
// Named distinctly from the SDK's `gap_event_handler` typedef, which it would
// otherwise shadow and make ambiguous at the point of use.
void dashcue_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    switch (event) {
        case ESP_GAP_BLE_PASSKEY_NOTIF_EVT: {
            // DisplayOnly (DEC-07): the HUD shows this 6-digit passkey; the phone
            // operator types it. Only shown while a pairing window is open.
            uint32_t passkey = param->ble_security.key_notif.passkey;
            // The six-digit secret belongs on the display, not in persistent or
            // externally collected UART logs.
            Serial.println("BLE: passkey notification received");
            Serial.flush();
            app_state_set_passkey(passkey);

            ProtoToUiMessage msg;
            memset(&msg, 0, sizeof(msg));
            msg.type = IPC_EVT_PASSKEY;
            msg.data.passkey.passkey = passkey;
            msg.data.passkey.timeout_s = app_state_get_pairing_remaining_s();
            ipc_send_proto_to_ui(&msg, 0);
            break;
        }

        case ESP_GAP_BLE_AUTH_CMPL_EVT: {
            if (param->ble_security.auth_cmpl.success) {
                // One bonded phone (DEC-07, §9.7): keep only the newest bond.
                int dev_num = esp_ble_get_bond_device_num();
                if (dev_num > 1) {
                    esp_ble_bond_dev_t* dev_list =
                        (esp_ble_bond_dev_t*)malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
                    if (dev_list != nullptr) {
                        int query_num = dev_num;
                        esp_ble_get_bond_device_list(&query_num, dev_list);
                        for (int i = 0; i < query_num; ++i) {
                            if (memcmp(dev_list[i].bd_addr, param->ble_security.auth_cmpl.bd_addr,
                                       sizeof(esp_bd_addr_t)) != 0) {
                                esp_ble_remove_bond_device(dev_list[i].bd_addr);
                            }
                        }
                        free(dev_list);
                    }
                }

                Serial.printf("BLE: AUTH COMPLETE success=%d\n", (int)param->ble_security.auth_cmpl.success);
                Serial.flush();
                esp_ble_gap_update_whitelist(true, param->ble_security.auth_cmpl.bd_addr,
                                             BLE_WL_ADDR_TYPE_PUBLIC);
                app_state_set_bonded(true, param->ble_security.auth_cmpl.bd_addr);

                if (s_pairing_active) {
                    s_pairing_active = false;
                    s_pairing_duration_ms = 0;
                    app_state_stop_pairing_window();
                }

                notify_ui_ble_state(HUD_STATE_CONNECTED_UNSYNCED, s_is_connected, s_negotiated_mtu);

                ProtoToUiMessage msg;
                memset(&msg, 0, sizeof(msg));
                msg.type = IPC_EVT_PASSKEY_DISMISSED;
                ipc_send_proto_to_ui(&msg, 0);
                ble_peripheral_update_advertising();
            } else {
                Serial.printf("BLE: AUTH FAILED fail_reason=%d\n", (int)param->ble_security.auth_cmpl.fail_reason);
                Serial.flush();
                // Failed authentication: stay pairable, report honestly.
                app_state_set_peripheral_state(HUD_STATE_ADVERTISING_PAIRING);
                ble_peripheral_update_advertising();
            }

            break;
        }

        case ESP_GAP_BLE_SEC_REQ_EVT: {
            // The stack verifies the configured Secure Connections + MITM pairing
            // before granting access to GATT attributes.
            Serial.println("BLE: Accepting security request for authenticated pairing");
            Serial.flush();
            esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
            break;
        }

        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            break;

        default:
            Serial.printf("BLE: GAP event %d\n", (int)event);
            Serial.flush();
            break;
    }
}

class BleServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override {
        uint16_t new_conn = param->connect.conn_id;

        // Single central (§9.6): a second connection is refused; a reconnect from
        // the bonded phone drops the stale link instead.
        if (s_conn_handle != 0xFFFF && s_conn_handle != new_conn) {
            pServer->disconnect(s_conn_handle);
        }

        s_conn_handle = new_conn;
        s_is_connected = true;

        // Link parameters (§9.1): 15-30 ms interval, latency 0, 4 s supervision.
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        conn_params.min_int = 12;  // 15 ms
        conn_params.max_int = 24;  // 30 ms
        conn_params.latency = 0;
        conn_params.timeout = 400; // 4 s
        esp_ble_gap_update_conn_params(&conn_params);

        // Request ATT MTU 247 (§9.1); the effective payload is MTU - 3 and the
        // protocol still works at MTU 23, just slower.
        BLEDevice::setMTU(247);

        app_state_set_connected(true, new_conn, s_negotiated_mtu);
        notify_ui_ble_state(HUD_STATE_CONNECTED_UNSYNCED, true, s_negotiated_mtu);

#if defined(DASHCUE_ENABLE_A2DP)
        dashcue_a2dp_connect_peer(param->connect.remote_bda);
#endif
    }

    void onDisconnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override {
        (void)pServer;
        (void)param;
        s_conn_handle = 0xFFFF;
        s_is_connected = false;
        s_negotiated_mtu = 23;

        app_state_set_connected(false, 0, s_negotiated_mtu);

        // Anti-ghost-state: a dropped link must not leave the HUD showing the
        // last track, the last turn cue, or a stale speed. Clearing here (on the
        // BLE task) rather than waiting for a UI tick means the panel is already
        // showing the idle state by the time the next frame is drawn.
        app_state_reset_session();
        artwork_store_invalidate();

        notify_ui_ble_state(HUD_STATE_ADVERTISING, false, s_negotiated_mtu);

        ble_peripheral_update_advertising();
    }

    void onMtuChanged(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override {
        (void)pServer;
        s_negotiated_mtu = param->mtu.mtu;
        app_state_set_mtu(s_negotiated_mtu);
        notify_ui_ble_state(app_state_get_peripheral_state(), s_is_connected, s_negotiated_mtu);
    }
};

class BleRxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param) override {
        (void)param;
        std::string value = pCharacteristic->getValue();
        size_t len = value.size();
        if (len > 0 && len <= 256) {
            BleRxPacket pkt;
            pkt.len = (uint16_t)len;
            memcpy(pkt.data, value.data(), len);
            if (g_ble_rx_queue != nullptr) {
                // A full queue is a counted fault, never a silent drop (§8.4).
                if (xQueueSend(g_ble_rx_queue, &pkt, 0) != pdPASS) {
                    app_state_inc_queue_drop();
                }
            }
        }
    }
};

static BleServerCallbacks s_server_callbacks;
static BleRxCallbacks     s_rx_callbacks;

// Each GATT service is heap cost on a board where the whole UI budget is ~50 KB:
// print it as the server is built so the cost is attributed, not guessed (§16).
void ble_heap_note(const char *stage) {
    Serial.printf("BLE heap after %-19s free=%u largest=%u\n",
                  stage,
                  (unsigned)esp_get_free_heap_size(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    Serial.flush();
}

} // namespace

bool ble_peripheral_init(void) {
    // Idempotent: called once from setup() (the NimBLE/BLE host creates its
    // inter-task primitives against the initialising task).
    if (s_peripheral_inited) {
        return true;
    }

    format_mac_strings();

    // The BT controller + host need a large contiguous allocation. Report the heap
    // before and after init: a silent hang here means the stack could not start,
    // and these two lines are what distinguishes "no memory" from "crashed".
    Serial.printf("BLE init: heap free=%u largest=%u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    Serial.flush();

    // Note on ordering: this is called from setup() *before* the LVGL screens are
    // built, because the BT stack needs large contiguous heap blocks and a heap
    // already chopped up by ~48 KB of LVGL objects cannot serve them
    // (esp_bluedroid_init rc=-1, which on the phone side looks like "no HUD here").
    //
#if defined(DASHCUE_ENABLE_A2DP)
    // Dual-Mode (BTDM): Classic BT enabled for A2DP music sink + BLE for HUD GATT telemetry
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        esp_err_t rc = esp_bt_controller_init(&bt_cfg);
        if (rc == ESP_OK) {
            rc = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
        }
        Serial.printf("BLE+A2DP: controller BTDM rc=%d heap free=%u\n",
                      (int)rc, (unsigned)esp_get_free_heap_size());
    }
#else
    // Classic-BT memory release: this product is strictly dedicated BLE-only.
    // Unconditionally release Classic BT controller memory before init to reclaim
    // ~35-40 KB contiguous DRAM directly into the heap for LVGL 9 display buffers and BLE GATT packets.
    esp_err_t rel_rc = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    Serial.printf("BLE: classic-BT mem release rc=%d heap free=%u\n",
                  (int)rel_rc, (unsigned)esp_get_free_heap_size());

    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        esp_err_t rc = esp_bt_controller_init(&bt_cfg);
        if (rc == ESP_OK) {
            rc = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        }
        Serial.printf("BLE: controller BLE-only rc=%d heap free=%u\n",
                      (int)rc, (unsigned)esp_get_free_heap_size());
    }
#endif
    Serial.flush();

    // BLEDevice::init() finds the controller already enabled and leaves it alone.
    BLEDevice::init("DashCue-HUD");

    if (!BLEDevice::getInitialized()) {
        Serial.println("BLE peripheral INIT FAILED - BT stack did not start (no memory?)");
        Serial.flush();
        return false;
    }

    Serial.printf("BLE stack up: heap free=%u largest=%u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    Serial.flush();
    BLEDevice::setPower(ESP_PWR_LVL_P9); // max TX power for the automotive cabin
    BLEDevice::setMTU(247);

    // Use Secure Connections + MITM and bonding. This HUD advertises DisplayOnly
    // capability and displays the stack-generated six-digit passkey.
    BLESecurity* pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    pSecurity->setCapability(ESP_IO_CAP_OUT);
    pSecurity->setKeySize(16);
    pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    BLEDevice::setCustomGapHandler(dashcue_gap_event_handler);

    s_pServer = BLEDevice::createServer();
    s_pServer->setCallbacks(&s_server_callbacks);
    ble_heap_note("gatt server");

    // 1. DashCue service: RX (phone -> HUD, write) and TX (HUD -> phone, notify)
    //
    // The Device Information Service (0x180A) and the alternate-UUID service were
    // removed: both cost heap (6,232 B measured) and neither is ever read. The
    // app declares DIS UUIDs in BleConstants.kt but no code path uses them, and
    // DASHCUE_SERVICE_UUID_ALT has no app-side constant at all. The firmware and
    // protocol revision are already carried in HELLO/DEVICE_STATUS, so nothing
    // the product reports was lost with them.
    BLEService* pDashCue = s_pServer->createService(BLEUUID(DASHCUE_SERVICE_UUID));

    s_pRxChar = pDashCue->createCharacteristic(
        BLEUUID(DASHCUE_RX_CHAR_UUID),
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    s_pRxChar->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
    s_pRxChar->setCallbacks(&s_rx_callbacks);

    s_pTxChar = pDashCue->createCharacteristic(
        BLEUUID(DASHCUE_TX_CHAR_UUID),
        BLECharacteristic::PROPERTY_NOTIFY);
    s_pTxChar->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
    BLE2902* pCccd = new BLE2902();
    pCccd->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
    s_pTxChar->addDescriptor(pCccd);

    pDashCue->start();
    ble_heap_note("DashCue service");

    // Accept-list from an existing bond (DEC-07: one bonded phone).
    // In Dual-Mode (BTDM), a bond may be recorded in BLE or Classic BT (A2DP).
    int num_bonds = esp_ble_get_bond_device_num();
#if defined(DASHCUE_ENABLE_A2DP)
    int num_classic_bonds = esp_bt_gap_get_bond_device_num();
#else
    int num_classic_bonds = 0;
#endif
    if (num_bonds > 0) {
        esp_ble_bond_dev_t* dev_list =
            (esp_ble_bond_dev_t*)malloc(sizeof(esp_ble_bond_dev_t) * num_bonds);
        if (dev_list != nullptr) {
            int query_num = num_bonds;
            esp_ble_get_bond_device_list(&query_num, dev_list);
            for (int i = 0; i < query_num; ++i) {
                // DEC-07: one bonded phone. Junk placeholders have been measured
                // in bond stores (A5:A5:A5:A5:A5:A5 and 83:95:8F:3E:F3:B9 on
                // 2026-10-26), so only structurally valid entries become "the
                // bonded phone" and enter the accept-list.
                if (app_state_is_valid_bt_addr(dev_list[i].bd_addr)) {
                    esp_ble_gap_update_whitelist(true, dev_list[i].bd_addr, BLE_WL_ADDR_TYPE_PUBLIC);
                    app_state_set_bonded(true, dev_list[i].bd_addr);
                    break;
                }
            }
            free(dev_list);
        }
    } else if (num_classic_bonds > 0) {
        esp_bd_addr_t* classic_list =
            (esp_bd_addr_t*)malloc(sizeof(esp_bd_addr_t) * num_classic_bonds);
        if (classic_list != nullptr) {
            int query_num = num_classic_bonds;
            esp_bt_gap_get_bond_device_list(&query_num, classic_list);
            for (int i = 0; i < query_num; ++i) {
                // Same junk-entry filter as the BLE branch above.
                if (app_state_is_valid_bt_addr(classic_list[i])) {
                    esp_ble_gap_update_whitelist(true, classic_list[i], BLE_WL_ADDR_TYPE_PUBLIC);
                    app_state_set_bonded(true, classic_list[i]);
                    break;
                }
            }
            free(classic_list);
        }
    } else {
        app_state_set_bonded(false, nullptr);
    }

    s_peripheral_inited = true;
    ble_peripheral_update_advertising();
    return true;
}

void ble_peripheral_start_pairing_window(uint32_t timeout_s) {
    if (!s_peripheral_inited) {
        return;
    }
    s_pairing_duration_ms = timeout_s * 1000u;
    s_pairing_start_ms = millis();
    s_pairing_active = true;
    app_state_start_pairing_window(timeout_s);
    ble_peripheral_update_advertising();
}

void ble_peripheral_cancel_pairing_window(void) {
    if (s_pairing_active) {
        s_pairing_active = false;
        s_pairing_duration_ms = 0;
        app_state_stop_pairing_window();
        if (!app_state_has_bonded_phone() && s_is_connected && s_pServer != nullptr) {
            s_pServer->disconnect(s_conn_handle);
        }
        ble_peripheral_update_advertising();
    }
}

void ble_peripheral_forget_phone(void) {
    int dev_num = esp_ble_get_bond_device_num();
    if (dev_num > 0) {
        esp_ble_bond_dev_t* dev_list =
            (esp_ble_bond_dev_t*)malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
        if (dev_list != nullptr) {
            int query_num = dev_num;
            esp_ble_get_bond_device_list(&query_num, dev_list);
            for (int i = 0; i < query_num; ++i) {
                esp_ble_remove_bond_device(dev_list[i].bd_addr);
                esp_ble_gap_update_whitelist(false, dev_list[i].bd_addr, BLE_WL_ADDR_TYPE_PUBLIC);
            }
            free(dev_list);
        }
    }
#if defined(DASHCUE_ENABLE_A2DP)
    int num_classic_bonds = esp_bt_gap_get_bond_device_num();
    if (num_classic_bonds > 0) {
        esp_bd_addr_t* classic_list =
            (esp_bd_addr_t*)malloc(sizeof(esp_bd_addr_t) * num_classic_bonds);
        if (classic_list != nullptr) {
            int query_num = num_classic_bonds;
            esp_bt_gap_get_bond_device_list(&query_num, classic_list);
            for (int i = 0; i < query_num; ++i) {
                esp_bt_gap_remove_bond_device(classic_list[i]);
            }
            free(classic_list);
        }
    }
    dashcue_a2dp_forget_peer();
#endif
    app_state_clear_bond();

    if (s_is_connected && s_pServer != nullptr) {
        s_pServer->disconnect(s_conn_handle);
    }

    ble_peripheral_cancel_pairing_window();
    ble_peripheral_update_advertising();
}

bool ble_peripheral_send_notify(const uint8_t *data, size_t length) {
    if (!s_is_connected || s_pTxChar == nullptr || data == nullptr || length == 0) {
        return false;
    }
    s_pTxChar->setValue((uint8_t*)data, length);
    s_pTxChar->notify();
    return true;
}

bool ble_peripheral_is_connected(void) {
    return s_is_connected;
}

uint16_t ble_peripheral_get_mtu(void) {
    return s_negotiated_mtu;
}

uint16_t ble_peripheral_get_conn_handle(void) {
    return s_conn_handle;
}

void ble_peripheral_update_advertising(void) {
    if (!s_peripheral_inited) {
        return;
    }

    BLEAdvertising* pAdv = BLEDevice::getAdvertising();
    if (pAdv == nullptr) {
        return;
    }

    pAdv->stop();

    if (s_is_connected) {
        return; // one central only: do not advertise while connected
    }

    // Advertise the service UUID; the name (when discoverable) goes in the scan
    // response because a 128-bit UUID plus a name would not fit one 31-byte PDU.
    BLEAdvertisementData advData;
#if defined(DASHCUE_ENABLE_A2DP)
    // Simultaneous LE and BR/EDR capable (Controller + Host) so Android recognizes Dual-Mode A2DP
    advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_DMT_CONTROLLER_SPT | ESP_BLE_ADV_FLAG_DMT_HOST_SPT);
#else
    advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
#endif
    advData.setCompleteServices(BLEUUID(DASHCUE_SERVICE_UUID));
    pAdv->setAdvertisementData(advData);
    pAdv->setMinPreferred(0x0C); // 15 ms
    pAdv->setMaxPreferred(0x18); // 30 ms

    BLEAdvertisementData scanResp;
    scanResp.setName("DashCue-HUD");
    pAdv->setScanResponse(true);
    pAdv->setScanResponseData(scanResp);
    pAdv->setScanFilter(false, false);
    pAdv->start();
    app_state_set_peripheral_state(HUD_STATE_ADVERTISING_PAIRING);
}

void ble_peripheral_tick(uint32_t now_ms) {
    if (s_pairing_active) {
        if (now_ms - s_pairing_start_ms >= s_pairing_duration_ms) {
            ble_peripheral_cancel_pairing_window();
        } else {
            app_state_tick_pairing_window();
        }
    }
}

void ble_peripheral_get_mac(uint8_t mac[6]) {
    esp_read_mac(mac, ESP_MAC_BT);
}

void ble_peripheral_get_device_id(char *out_id, size_t max_len) {
    if (out_id == nullptr || max_len == 0) return;
    strncpy(out_id, s_device_id_str, max_len - 1);
    out_id[max_len - 1] = '\0';
}

#endif // DASHCUE_NATIVE_TEST
