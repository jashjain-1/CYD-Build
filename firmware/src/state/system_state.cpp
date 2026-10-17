#include "system_state.h"
#include <stdint.h>
#include <stdbool.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_timer.h>
#else
#include <chrono>
#define RTC_DATA_ATTR
static inline uint64_t get_host_time_ms() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
#define millis() ((uint32_t)get_host_time_ms())
static inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
static inline int64_t esp_timer_get_time() { return (int64_t)get_host_time_ms() * 1000LL; }
#endif

#define SAFEMODE_RTC_MAGIC 0x5AFE0001

RTC_DATA_ATTR static RtcCrashTracker s_rtc_crash_tracker;

static bool s_is_safemode = false;
static uint32_t s_boot_time_s = 0;
static bool s_stable_cleared = false;

// Link & Speed State
static bool s_ble_connected = false;
static bool s_ble_authenticated = false;
static uint16_t s_ble_mtu = 23;

static int32_t s_speed_kmh_x10 = -1; // -1 = unknown
static uint32_t s_last_speed_update_ms = 0;
static uint32_t s_last_phone_packet_ms = 0;
static uint32_t s_last_nav_update_ms = 0;
static uint32_t s_last_media_update_ms = 0;

static bool s_driving_locked = false;
static bool s_phone_disconnected_banner = true;

void system_state_boot_check(void) {
    uint32_t now_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    s_boot_time_s = now_s;
    s_stable_cleared = false;

    esp_reset_reason_t reason = esp_reset_reason();

    if (reason == ESP_RST_POWERON) {
        // Cold power-on reset clears crash window
        s_rtc_crash_tracker.magic = SAFEMODE_RTC_MAGIC;
        s_rtc_crash_tracker.reset_count = 0;
        s_rtc_crash_tracker.first_reset_time_s = now_s;
        s_rtc_crash_tracker.last_reset_reason = (uint32_t)reason;
        s_is_safemode = false;
    } else {
        // Warm reset (watchdog, panic, software reset)
        if (s_rtc_crash_tracker.magic == SAFEMODE_RTC_MAGIC) {
            if ((now_s >= s_rtc_crash_tracker.first_reset_time_s) &&
                (now_s - s_rtc_crash_tracker.first_reset_time_s <= SAFEMODE_RESET_WINDOW_S)) {
                s_rtc_crash_tracker.reset_count++;
            } else {
                s_rtc_crash_tracker.reset_count = 1;
                s_rtc_crash_tracker.first_reset_time_s = now_s;
            }
        } else {
            // First time or RTC memory uninitialized
            s_rtc_crash_tracker.magic = SAFEMODE_RTC_MAGIC;
            s_rtc_crash_tracker.reset_count = 1;
            s_rtc_crash_tracker.first_reset_time_s = now_s;
        }
        s_rtc_crash_tracker.last_reset_reason = (uint32_t)reason;

        if (s_rtc_crash_tracker.reset_count >= SAFEMODE_RESET_THRESHOLD) {
            s_is_safemode = true;
        } else {
            s_is_safemode = false;
        }
    }
}

bool system_state_is_safemode(void) {
    return s_is_safemode;
}

uint32_t system_state_get_reset_count(void) {
    return s_rtc_crash_tracker.reset_count;
}

esp_reset_reason_t system_state_get_last_reset_reason(void) {
    return (esp_reset_reason_t)s_rtc_crash_tracker.last_reset_reason;
}

void system_state_clear_safemode(void) {
    s_rtc_crash_tracker.reset_count = 0;
    s_is_safemode = false;
}

void system_state_update_speed(int32_t speed_kmh_x10, uint32_t now_ms) {
    s_speed_kmh_x10 = speed_kmh_x10;
    s_last_speed_update_ms = now_ms;
    s_last_phone_packet_ms = now_ms;
    system_state_tick(now_ms);
}

void system_state_feed_phone_link(uint32_t now_ms) {
    s_last_phone_packet_ms = now_ms;
    system_state_tick(now_ms);
}

void system_state_set_ble_connected(bool connected, bool authenticated, uint16_t mtu) {
    s_ble_connected = connected;
    s_ble_authenticated = authenticated;
    if (connected && mtu >= 23) {
        s_ble_mtu = mtu;
    }
    if (!connected) {
        s_speed_kmh_x10 = -1; // Reset speed on disconnect
    }
    system_state_tick(millis());
}

void system_state_tick(uint32_t now_ms) {
    uint32_t now_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    // Stable run reset: if firmware has run > 60 s without crashing, reset tracker
    if (!s_stable_cleared && (now_s - s_boot_time_s >= SAFEMODE_RESET_WINDOW_S)) {
        s_rtc_crash_tracker.reset_count = 0;
        s_stable_cleared = true;
    }

    // Link liveness evaluation (SI-05)
    bool link_active = s_ble_connected &&
                       s_ble_authenticated &&
                       (s_last_phone_packet_ms > 0) &&
                       ((now_ms - s_last_phone_packet_ms) <= STALE_PHONE_TIMEOUT_MS);

    if (!link_active) {
        // Link stale or disconnected: show banner, unlock settings (DEC-28 / SI-05)
        s_phone_disconnected_banner = true;
        s_driving_locked = false;
    } else {
        s_phone_disconnected_banner = false;

        // Check speed lock
        if (s_speed_kmh_x10 >= 0 && (now_ms - s_last_speed_update_ms) <= STALE_PHONE_TIMEOUT_MS) {
            if (s_speed_kmh_x10 >= DRIVING_LOCK_SPEED_KMH_X10_THRESHOLD) {
                s_driving_locked = true;
            } else {
                s_driving_locked = false;
            }
        } else {
            // Speed unknown (-1) or speed reading stale: treat as not moving (SI-05)
            s_driving_locked = false;
        }
    }
}

bool system_state_is_driving_locked(void) {
    return s_driving_locked;
}

bool system_state_is_settings_accessible(void) {
    // Accessible only when NOT locked and NOT in SafeMode
    return !s_driving_locked && !s_is_safemode;
}

bool system_state_is_phone_disconnected_banner(void) {
    return s_phone_disconnected_banner;
}

bool system_state_can_switch_screen(HudScreenId target_screen) {
    if (s_is_safemode) {
        return (target_screen == HUD_SCREEN_SAFEMODE);
    }
    if (s_driving_locked) {
        // Settings and Calibration blocked while driving (>= 8 km/h)
        if (target_screen == HUD_SCREEN_SETTINGS || target_screen == HUD_SCREEN_CALIBRATION) {
            return false;
        }
    }
    return true;
}

bool system_state_is_nav_stale(uint32_t now_ms) {
    if (s_last_nav_update_ms == 0) return true;
    return ((now_ms - s_last_nav_update_ms) > STALE_NAV_TIMEOUT_MS);
}

bool system_state_is_media_stale(uint32_t now_ms) {
    if (s_last_media_update_ms == 0) return true;
    return ((now_ms - s_last_media_update_ms) > STALE_MEDIA_TIMEOUT_MS);
}

int32_t system_state_get_speed_kmh_x10(void) {
    return s_speed_kmh_x10;
}
