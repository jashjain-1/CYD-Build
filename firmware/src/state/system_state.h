#ifndef DASHCUE_STATE_SYSTEM_STATE_H
#define DASHCUE_STATE_SYSTEM_STATE_H

#include <stdint.h>
#include <stdbool.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_system.h>
#else
typedef enum {
    ESP_RST_UNKNOWN = 0,
    ESP_RST_POWERON,
    ESP_RST_EXT,
    ESP_RST_SW,
    ESP_RST_PANIC,
    ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT,
    ESP_RST_WDT,
    ESP_RST_DEEPSLEEP,
    ESP_RST_BROWNOUT,
    ESP_RST_SDIO
} esp_reset_reason_t;
#endif

#include "protocol/generated/dashcue.pb.h"
#include "ipc_messages.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DRIVING_LOCK_SPEED_KMH_X10_THRESHOLD 80  // 8.0 km/h (DEC-28)
#define STALE_PHONE_TIMEOUT_MS               6000 // 6 s (SI-05 / §9.6)
#define STALE_NAV_TIMEOUT_MS                 5000 // 5 s (TC-FW-05)
#define STALE_MEDIA_TIMEOUT_MS               6000 // 6 s (TC-FW-05)
#define SAFEMODE_RESET_THRESHOLD             3    // >= 3 resets within 60 s (TC-HW-11)
#define SAFEMODE_RESET_WINDOW_S              60   // 60-second window

typedef enum {
    HUD_SCREEN_MEDIA = 0,
    HUD_SCREEN_NAV = 1,
    HUD_SCREEN_MAP = 2,
    HUD_SCREEN_SETTINGS = 3,
    HUD_SCREEN_DIAGNOSTICS = 4,
    HUD_SCREEN_CALIBRATION = 5,
    HUD_SCREEN_SAFEMODE = 6
} HudScreenId;

typedef struct {
    uint32_t magic;
    uint32_t reset_count;
    uint32_t first_reset_time_s;
    uint32_t last_reset_reason;
} RtcCrashTracker;

/**
 * Boot-time check for watchdog/panic crash frequency in RTC slow memory.
 * Sets SafeMode active if >= 3 resets occurred within 60 s.
 */
void system_state_boot_check(void);

/**
 * Query whether system is currently in SafeMode (TC-HW-11).
 */
bool system_state_is_safemode(void);

/**
 * Get crash tracker reset count.
 */
uint32_t system_state_get_reset_count(void);

/**
 * Get recorded last reset reason.
 */
esp_reset_reason_t system_state_get_last_reset_reason(void);

/**
 * Manually reset SafeMode crash tracker (e.g. factory reset or clean exit).
 */
void system_state_clear_safemode(void);

/**
 * Update vehicle speed from Heartbeat or NavState.
 * @param speed_kmh_x10 Speed in km/h * 10, or -1 if unknown (DEC-28).
 * @param now_ms Current monotonic clock timestamp in ms.
 */
void system_state_update_speed(int32_t speed_kmh_x10, uint32_t now_ms);

/**
 * Mark receipt of a packet from the phone to keep the link active.
 * @param now_ms Current monotonic clock timestamp in ms.
 */
void system_state_feed_phone_link(uint32_t now_ms);

/**
 * Set BLE link physical/security connection status.
 */
void system_state_set_ble_connected(bool connected, bool authenticated, uint16_t mtu);

/**
 * Periodic tick to check speed timeout, stale link, and driving lock state.
 * @param now_ms Current monotonic clock timestamp in ms.
 */
void system_state_tick(uint32_t now_ms);

/**
 * Query whether driving lock is currently engaged (DEC-28).
 * Speed >= 8 km/h locks Settings and complex distraction surfaces.
 */
bool system_state_is_driving_locked(void);

/**
 * Query whether settings entry is permitted.
 * Returns true if car is stationary (speed < 8 km/h), speed unknown (-1),
 * or phone disconnected (SI-05).
 */
bool system_state_is_settings_accessible(void);

/**
 * Query whether persistent "Phone Disconnected" banner should be displayed (SI-05).
 */
bool system_state_is_phone_disconnected_banner(void);

/**
 * Screen transition permission checker.
 * Returns true if navigation to target screen is allowed under current driving lock & safemode.
 */
bool system_state_can_switch_screen(HudScreenId target_screen);

/**
 * Check if nav state is stale (> 5 s).
 */
bool system_state_is_nav_stale(uint32_t now_ms);

/**
 * Check if media state is stale (> 6 s).
 */
bool system_state_is_media_stale(uint32_t now_ms);

/**
 * Get last reported speed in km/h * 10 (-1 if unknown).
 */
int32_t system_state_get_speed_kmh_x10(void);

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_STATE_SYSTEM_STATE_H
