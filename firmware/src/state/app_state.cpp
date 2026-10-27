#include "app_state.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifndef DASHCUE_NATIVE_TEST
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#define RTC_DATA RTC_DATA_ATTR
#else
#define RTC_DATA
#endif

#define RTC_CRASH_MAGIC 0x5AFE0001u

namespace {
typedef struct {
    uint32_t magic;
    uint32_t reset_count;
    uint32_t first_reset_s;
    uint32_t last_reset_reason;
} AppStateCrashTracker;

RTC_DATA static AppStateCrashTracker s_rtc_crash_tracker;
}

// BLE raw packet queue
QueueHandle_t g_ble_rx_queue = NULL;

#ifndef DASHCUE_NATIVE_TEST
static SemaphoreHandle_t s_state_mutex = NULL;
#endif

// Internal state structure
typedef struct {
    // Peripheral & connection
    HudPeripheralState peripheral_state;
    bool is_connected;
    uint16_t conn_handle;
    uint16_t negotiated_mtu;
    int32_t rssi_dbm;

    // Pairing & security
    bool is_pairing_window_active;
    uint32_t pairing_remaining_s;
    uint32_t pairing_passkey;
    bool has_bonded_phone;
    uint8_t bonded_phone_addr[6];

    // Driving Safety & Speed
    int32_t speed_kmh_x10;
    bool driving_lock_active;

    // Freshness & Heartbeat
    uint32_t last_heartbeat_rx_ms;
    uint32_t last_nav_rx_ms;
    uint32_t last_media_rx_ms;
    uint32_t missed_heartbeats;
    bool phone_stale;
    bool nav_stale;
    bool media_stale;

    // SafeMode
    bool safe_mode_active;
    uint32_t crash_reset_count;

    // Diagnostics counters
    uint32_t boot_time_s;
    uint32_t crc_errors;
    uint32_t queue_drops;
    uint32_t frag_timeouts;
    uint32_t duplicate_commands;

    // State snapshots
    dashcue_v1_MediaState media;
    dashcue_v1_NavState nav;
    // No route copy is kept: the HUD no longer renders a schematic map, so a
    // 120-point RouteOverview would be ~1 KB of dead BSS. The phone owns the
    // geometry; the HUD only receives the active NavState cue.
    dashcue_v1_SettingsData settings;
} InternalAppState;

static InternalAppState s_state;

void app_state_lock(void) {
#ifndef DASHCUE_NATIVE_TEST
    if (s_state_mutex != NULL) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    }
#endif
}

void app_state_unlock(void) {
#ifndef DASHCUE_NATIVE_TEST
    if (s_state_mutex != NULL) {
        xSemaphoreGive(s_state_mutex);
    }
#endif
}

bool app_state_init(void) {
    memset(&s_state, 0, sizeof(s_state));

    s_state.peripheral_state = HUD_STATE_BOOTING;
    s_state.negotiated_mtu = 23;
    s_state.speed_kmh_x10 = -1; // unknown
    s_state.driving_lock_active = false;
    s_state.phone_stale = true;
    s_state.nav_stale = true;
    s_state.media_stale = true;

    // Default settings
    s_state.settings.brightness_pct = 80;
    s_state.settings.theme = dashcue_v1_ThemeMode_TH_DARK;
    s_state.settings.orientation = dashcue_v1_ScreenOrientation_SO_LANDSCAPE;
    s_state.settings.left_hand_traffic = false;
    s_state.settings.seek_step_s = 10;
    s_state.settings.units = dashcue_v1_UnitSystem_UN_METRIC;
    s_state.settings.version = 1;

#ifndef DASHCUE_NATIVE_TEST
    if (s_state_mutex == NULL) {
        s_state_mutex = xSemaphoreCreateMutex();
    }
    if (g_proto_to_ui_queue == NULL) {
        g_proto_to_ui_queue = xQueueCreate(PROTO_TO_UI_QUEUE_LEN, sizeof(ProtoToUiMessage));
    }
    if (g_ui_to_proto_queue == NULL) {
        g_ui_to_proto_queue = xQueueCreate(UI_TO_PROTO_QUEUE_LEN, sizeof(UiToProtoMessage));
    }
    if (g_ble_rx_queue == NULL) {
        g_ble_rx_queue = xQueueCreate(BLE_RX_QUEUE_LEN, sizeof(BleRxPacket));
    }
    return s_state_mutex != NULL && g_proto_to_ui_queue != NULL &&
           g_ui_to_proto_queue != NULL && g_ble_rx_queue != NULL;
#else
    return true;
#endif
}

HudPeripheralState app_state_get_peripheral_state(void) {
    app_state_lock();
    HudPeripheralState st = s_state.peripheral_state;
    app_state_unlock();
    return st;
}

void app_state_set_peripheral_state(HudPeripheralState state) {
    app_state_lock();
    s_state.peripheral_state = state;
    app_state_unlock();
}

bool app_state_is_connected(void) {
    app_state_lock();
    bool c = s_state.is_connected;
    app_state_unlock();
    return c;
}

void app_state_set_connected(bool connected, uint16_t handle, uint16_t mtu) {
    app_state_lock();
    s_state.is_connected = connected;
    s_state.conn_handle = handle;
    if (mtu >= 23) {
        s_state.negotiated_mtu = mtu;
    }
    if (!connected) {
        s_state.phone_stale = true;
        s_state.nav_stale = true;
        s_state.media_stale = true;
        s_state.driving_lock_active = false;
        s_state.peripheral_state = HUD_STATE_ADVERTISING;
    } else {
        s_state.peripheral_state = HUD_STATE_CONNECTED_UNSYNCED;
    }
    app_state_unlock();
}

uint16_t app_state_get_mtu(void) {
    app_state_lock();
    uint16_t m = s_state.negotiated_mtu;
    app_state_unlock();
    return m;
}

void app_state_set_mtu(uint16_t mtu) {
    app_state_lock();
    s_state.negotiated_mtu = mtu;
    app_state_unlock();
}

void app_state_set_rssi(int32_t rssi_dbm) {
    app_state_lock();
    s_state.rssi_dbm = rssi_dbm;
    app_state_unlock();
}

void app_state_start_pairing_window(uint32_t timeout_s) {
    app_state_lock();
    s_state.is_pairing_window_active = true;
    s_state.pairing_remaining_s = timeout_s;
    s_state.peripheral_state = HUD_STATE_ADVERTISING_PAIRING;
    app_state_unlock();
}

void app_state_stop_pairing_window(void) {
    app_state_lock();
    s_state.is_pairing_window_active = false;
    s_state.pairing_remaining_s = 0;
    s_state.pairing_passkey = 0;
    if (s_state.peripheral_state == HUD_STATE_ADVERTISING_PAIRING) {
        s_state.peripheral_state = HUD_STATE_ADVERTISING;
    }
    app_state_unlock();
}

bool app_state_is_pairing_window_active(void) {
    app_state_lock();
    bool active = s_state.is_pairing_window_active;
    app_state_unlock();
    return active;
}

uint32_t app_state_get_pairing_remaining_s(void) {
    app_state_lock();
    uint32_t rem = s_state.pairing_remaining_s;
    app_state_unlock();
    return rem;
}

void app_state_tick_pairing_window(void) {
    app_state_lock();
    if (s_state.is_pairing_window_active) {
        if (s_state.pairing_remaining_s > 0) {
            s_state.pairing_remaining_s--;
        }
        if (s_state.pairing_remaining_s == 0) {
            s_state.is_pairing_window_active = false;
            s_state.pairing_passkey = 0;
            if (s_state.peripheral_state == HUD_STATE_ADVERTISING_PAIRING) {
                s_state.peripheral_state = HUD_STATE_ADVERTISING;
            }
        }
    }
    app_state_unlock();
}

void app_state_set_passkey(uint32_t passkey) {
    app_state_lock();
    s_state.pairing_passkey = passkey;
    app_state_unlock();
}

uint32_t app_state_get_passkey(void) {
    app_state_lock();
    uint32_t pk = s_state.pairing_passkey;
    app_state_unlock();
    return pk;
}

bool app_state_has_bonded_phone(void) {
    app_state_lock();
    bool b = s_state.has_bonded_phone;
    app_state_unlock();
    return b;
}

const uint8_t *app_state_get_bonded_addr(void) {
    app_state_lock();
    if (!s_state.has_bonded_phone) {
        app_state_unlock();
        return NULL;
    }
    static uint8_t addr_snapshot[6];
    memcpy(addr_snapshot, s_state.bonded_phone_addr, 6);
    app_state_unlock();
    return addr_snapshot;
}

void app_state_set_bonded(bool bonded, const uint8_t *mac) {
    app_state_lock();
    s_state.has_bonded_phone = bonded;
    if (mac != NULL) {
        memcpy(s_state.bonded_phone_addr, mac, 6);
    } else {
        memset(s_state.bonded_phone_addr, 0, 6);
    }
    app_state_unlock();
}

bool app_state_is_valid_bt_addr(const uint8_t *bda) {
    if (bda == NULL) return false;
    // All-zero is the cleared bond, all-0xFF is the unset read, and a block of
    // one repeated octet is a test filler (A5:A5:A5:A5:A5:A5 was measured in
    // the GAP bond list on 2026-10-26). No allocated OUI repeats one byte
    // across all six octets, so the identical-bytes rule cannot reject a phone.
    // The Espressif demo/test filler 00:07:00:00:00:07 (its 00:07 OUI plus a
    // repeated low octet) is accepted by the structural rule above, so it is
    // rejected explicitly: no consumer handset shares this exact address.
    static const uint8_t espressif_filler[6] = {0x00, 0x07, 0x00, 0x00, 0x00, 0x07};
    if (memcmp(bda, espressif_filler, 6) == 0) return false;
    int identical = 1;
    for (int i = 0; i < 6; ++i) {
        if (bda[i] != bda[0]) identical = 0;
    }
    return !identical;
}

void app_state_clear_bond(void) {
    app_state_lock();
    s_state.has_bonded_phone = false;
    memset(s_state.bonded_phone_addr, 0, 6);
    app_state_unlock();
}

static void update_driving_lock_locked(void) {
    // Speed >= 8.0 km/h (speed_kmh_x10 >= 80) engages driving lock
    // Speed unknown (-1) or phone stale (> 6s) disengages driving lock (DEC-28 / SI-05)
    if (s_state.phone_stale || s_state.speed_kmh_x10 < 0 || s_state.speed_kmh_x10 < 80) {
        s_state.driving_lock_active = false;
    } else {
        s_state.driving_lock_active = true;
    }
}

void app_state_on_heartbeat_rx(const dashcue_v1_Heartbeat *hb, uint32_t now_ms) {
    if (hb == NULL) return;
    app_state_lock();
    s_state.last_heartbeat_rx_ms = now_ms;
    s_state.missed_heartbeats = 0;
    s_state.phone_stale = false;

    if (hb->speed_kmh_x10 >= -1) {
        s_state.speed_kmh_x10 = hb->speed_kmh_x10;
    }

    if (s_state.peripheral_state == HUD_STATE_STALE || s_state.peripheral_state == HUD_STATE_CONNECTED_UNSYNCED) {
        s_state.peripheral_state = HUD_STATE_READY;
    }

    update_driving_lock_locked();
    app_state_unlock();
}

void app_state_on_media_state(const dashcue_v1_MediaState *ms, uint32_t now_ms) {
    if (ms == NULL) return;
    app_state_lock();
    s_state.last_media_rx_ms = now_ms;
    s_state.media_stale = false;
    memcpy(&s_state.media, ms, sizeof(dashcue_v1_MediaState));
    app_state_unlock();
}

void app_state_on_nav_state(const dashcue_v1_NavState *ns, uint32_t now_ms) {
    if (ns == NULL) return;
    app_state_lock();
    s_state.last_nav_rx_ms = now_ms;
    s_state.nav_stale = false;
    memcpy(&s_state.nav, ns, sizeof(dashcue_v1_NavState));

    if (ns->speed_kmh_x10 >= -1) {
        s_state.speed_kmh_x10 = ns->speed_kmh_x10;
        update_driving_lock_locked();
    }
    app_state_unlock();
}

void app_state_on_settings_set(const dashcue_v1_SettingsData *s) {
    if (s == NULL) return;
    app_state_lock();
    memcpy(&s_state.settings, s, sizeof(dashcue_v1_SettingsData));
    app_state_unlock();
}

void app_state_get_settings(dashcue_v1_SettingsData *out_settings) {
    if (out_settings == NULL) return;
    app_state_lock();
    memcpy(out_settings, &s_state.settings, sizeof(dashcue_v1_SettingsData));
    app_state_unlock();
}

void app_state_reset_session(void) {
    app_state_lock();

    memset(&s_state.media, 0, sizeof(s_state.media));
    memset(&s_state.nav, 0, sizeof(s_state.nav));
    s_state.media.status = dashcue_v1_PlaybackStatus_PB_NO_SESSION;
    s_state.nav.status = dashcue_v1_NavStatus_NAV_NONE;

    // Zero the freshness anchors as well as the flags: a non-zero anchor would
    // let the first post-reconnect tick compute a "stale" interval from the
    // previous drive and immediately re-stale the new session.
    s_state.last_heartbeat_rx_ms = 0;
    s_state.last_nav_rx_ms = 0;
    s_state.last_media_rx_ms = 0;
    s_state.missed_heartbeats = 0;
    s_state.phone_stale = true;
    s_state.nav_stale = true;
    s_state.media_stale = true;
    s_state.speed_kmh_x10 = -1;
    s_state.driving_lock_active = false;

    app_state_unlock();
}

void app_state_check_freshness(uint32_t now_ms) {
    app_state_lock();
    if (s_state.is_connected) {
        // Heartbeat / Phone freshness: 3 missed heartbeats (6 s) = stale
        if (s_state.last_heartbeat_rx_ms > 0 &&
            (now_ms - s_state.last_heartbeat_rx_ms >= STALE_PHONE_MS)) {
            s_state.phone_stale = true;
            s_state.missed_heartbeats = LINK_DEAD_MISSES;
            if (s_state.peripheral_state == HUD_STATE_READY) {
                s_state.peripheral_state = HUD_STATE_STALE;
            }
        }

        // Nav freshness: 5 s without update
        if (s_state.last_nav_rx_ms > 0 &&
            (now_ms - s_state.last_nav_rx_ms >= STALE_NAV_MS)) {
            s_state.nav_stale = true;
        }

        // Media freshness:
        // When playback is actively playing and phone link is healthy, media is continuously valid.
        if (s_state.last_media_rx_ms > 0) {
            bool actively_playing = (s_state.media.status == dashcue_v1_PlaybackStatus_PB_PLAYING);
            if (actively_playing && !s_state.phone_stale) {
                s_state.media_stale = false;
            } else if (now_ms - s_state.last_media_rx_ms >= STALE_MEDIA_MS) {
                s_state.media_stale = true;
            }
        }

        update_driving_lock_locked();
    }
    app_state_unlock();
}

bool app_state_is_driving_locked(void) {
    app_state_lock();
    bool locked = s_state.driving_lock_active;
    app_state_unlock();
    return locked;
}

bool app_state_is_phone_stale(void) {
    app_state_lock();
    bool stale = s_state.phone_stale;
    app_state_unlock();
    return stale;
}

bool app_state_is_nav_stale(void) {
    app_state_lock();
    bool stale = s_state.nav_stale;
    app_state_unlock();
    return stale;
}

bool app_state_is_media_stale(void) {
    app_state_lock();
    bool stale = s_state.media_stale;
    app_state_unlock();
    return stale;
}

int32_t app_state_get_speed_kmh_x10(void) {
    app_state_lock();
    int32_t sp = s_state.speed_kmh_x10;
    app_state_unlock();
    return sp;
}

void app_state_record_boot(uint32_t reset_reason, uint32_t now_s) {
    app_state_lock();
    s_state.boot_time_s = now_s;

    if (s_rtc_crash_tracker.magic != RTC_CRASH_MAGIC) {
        s_rtc_crash_tracker.magic = RTC_CRASH_MAGIC;
        s_rtc_crash_tracker.reset_count = 0;
        s_rtc_crash_tracker.first_reset_s = now_s;
    }

#ifndef DASHCUE_NATIVE_TEST
    // ESP_RST_POWERON = 1
    const bool is_power_on = (reset_reason == 1);
#else
    const bool is_power_on = false;
#endif

    if (!is_power_on) {
        if (now_s >= s_rtc_crash_tracker.first_reset_s &&
            (now_s - s_rtc_crash_tracker.first_reset_s <= 60)) {
            s_rtc_crash_tracker.reset_count++;
        } else {
            s_rtc_crash_tracker.reset_count = 1;
            s_rtc_crash_tracker.first_reset_s = now_s;
        }
    } else {
        s_rtc_crash_tracker.reset_count = 0;
        s_rtc_crash_tracker.first_reset_s = now_s;
    }

    s_rtc_crash_tracker.last_reset_reason = reset_reason;
    s_state.crash_reset_count = s_rtc_crash_tracker.reset_count;

    if (s_rtc_crash_tracker.reset_count >= 3) {
        s_state.safe_mode_active = true;
        s_state.peripheral_state = HUD_STATE_SAFE_MODE;
    } else {
        s_state.safe_mode_active = false;
    }
    app_state_unlock();
}

void app_state_mark_stable_run(void) {
    app_state_lock();
    s_rtc_crash_tracker.reset_count = 0;
    s_state.crash_reset_count = 0;
    app_state_unlock();
}

bool app_state_is_safe_mode(void) {
    app_state_lock();
    bool sm = s_state.safe_mode_active;
    app_state_unlock();
    return sm;
}

uint32_t app_state_get_crash_count(void) {
    app_state_lock();
    uint32_t cc = s_state.crash_reset_count;
    app_state_unlock();
    return cc;
}

void app_state_get_media_snapshot(dashcue_v1_MediaState *out_ms) {
    if (out_ms == NULL) return;
    app_state_lock();
    memcpy(out_ms, &s_state.media, sizeof(dashcue_v1_MediaState));
    app_state_unlock();
}

void app_state_get_nav_snapshot(dashcue_v1_NavState *out_ns) {
    if (out_ns == NULL) return;
    app_state_lock();
    memcpy(out_ns, &s_state.nav, sizeof(dashcue_v1_NavState));
    app_state_unlock();
}

void app_state_get_device_status(dashcue_v1_DeviceStatus *out_status, uint32_t now_s) {
    if (out_status == NULL) return;
    app_state_lock();
    memset(out_status, 0, sizeof(dashcue_v1_DeviceStatus));
    out_status->uptime_s = (now_s >= s_state.boot_time_s) ? (now_s - s_state.boot_time_s) : now_s;
    out_status->reset_count = s_state.crash_reset_count;
    out_status->crc_failures = s_state.crc_errors;
    out_status->queue_drops = s_state.queue_drops;
    out_status->frag_timeouts = s_state.frag_timeouts;
    out_status->duplicate_commands = s_state.duplicate_commands;
    out_status->mtu = s_state.negotiated_mtu;
    out_status->rssi_dbm = s_state.rssi_dbm;
    out_status->safe_mode = s_state.safe_mode_active;

#ifndef DASHCUE_NATIVE_TEST
    out_status->min_free_heap = ESP.getMinFreeHeap();
    out_status->largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#else
    out_status->min_free_heap = 160000;
    out_status->largest_free_block = 120000;
#endif
    app_state_unlock();
}

void app_state_inc_crc_error(void) {
    app_state_lock();
    s_state.crc_errors++;
    app_state_unlock();
}

void app_state_inc_queue_drop(void) {
    app_state_lock();
    s_state.queue_drops++;
    app_state_unlock();
}

void app_state_inc_frag_timeout(void) {
    app_state_lock();
    s_state.frag_timeouts++;
    app_state_unlock();
}

void app_state_inc_dup_command(void) {
    app_state_lock();
    s_state.duplicate_commands++;
    app_state_unlock();
}
