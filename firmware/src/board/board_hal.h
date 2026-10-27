#ifndef DASHCUE_BOARD_HAL_H
#define DASHCUE_BOARD_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
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
typedef void* SemaphoreHandle_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Shared SPI bus mutex between TFT_eSPI DMA flush and XPT2046 touch sampling.
 * Initialized in board_init().
 */
extern SemaphoreHandle_t g_spi_bus_mutex;

/**
 * Initialize board GPIOs, PWM backlight on GPIO 27, status LEDs, LDR,
 * and shared SPI bus mutex g_spi_bus_mutex.
 */
void board_init(void);

/**
 * Set LCD backlight brightness percentage (clamped to 10% .. 100%).
 * Uses LEDC PWM at 5 kHz, 8-bit resolution (channel 0).
 */
void board_set_backlight_pct(uint8_t pct);

/**
 * Get currently configured backlight percentage (10 .. 100).
 */
uint8_t board_get_backlight_pct(void);

/**
 * Read ambient light level from onboard LDR (analog on GPIO 34).
 */
uint16_t board_read_ambient_light(void);

/**
 * Retrieve stable device MAC address from ESP32 efuse.
 */
void board_get_mac(uint8_t mac[6]);

/**
 * Format stable device ID string (e.g. "DASHCUE-D4E9F46FF088").
 */
void board_get_device_id(char *out_id, size_t max_len);

/**
 * Get reset reason from hardware.
 */
esp_reset_reason_t board_get_reset_reason(void);

/**
 * Convert reset reason enum to human-readable string.
 */
const char* board_get_reset_reason_str(esp_reset_reason_t reason);

/**
 * Query current free heap in internal RAM.
 */
uint32_t board_get_free_heap(void);

/**
 * Query minimum free heap observed since boot.
 */
uint32_t board_get_min_free_heap(void);

/**
 * Query largest contiguous free block in heap.
 */
uint32_t board_get_largest_free_block(void);

/**
 * Set RGB status LED state (active LOW on CYD).
 */
void board_set_led_rgb(bool r, bool g, bool b);

/**
 * True while the board's BOOT button is held (active LOW). This is the only
 * operator input that survives a dead touch overlay, so the HUD uses it to step
 * through the tabs.
 */
bool board_button_pressed(void);

/**
 * Trigger software reset of the MCU.
 */
void board_reboot(void);

/**
 * Backlight brightness helpers (alias for backlight_pct).
 */
uint8_t board_get_backlight_brightness(void);
void board_set_backlight_brightness(uint8_t pct);

/**
 * Crash and SafeMode tracker helpers.
 */
bool board_is_safemode_triggered(void);
uint32_t board_get_reset_count(void);
uint32_t board_get_last_reset_reason(void);
void board_reset_crash_tracker(void);

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_BOARD_HAL_H
