#ifndef DASHCUE_TOUCH_HAL_H
#define DASHCUE_TOUCH_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "calibration.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(ESP_PLATFORM) || defined(ARDUINO)
extern SemaphoreHandle_t g_spi_bus_mutex;
#endif

// 3,584 B: provides robust stack headroom during extended soak testing (was 2,048)
// where continuous SPI driver operations, interrupts and serial logging can spike.
#define TOUCH_TASK_STACK_SIZE 3584u
#define TOUCH_TASK_PRIORITY   5u
#define TOUCH_TASK_CORE_ID    1u

/**
 * Initialize shared SPI bus for touch controller (pins 14/13/12/33/36),
 * attempt to load NVS calibration blob, verify CRC32 and residual bounds.
 */
void touch_hal_setup(void);

/**
 * Start the 100 Hz touch sampling task on Core 1 (priority 5).
 */
bool touch_hal_start_task(void);

/**
 * Check if touch subsystem has a valid, verified calibration blob.
 * (TC-HW-04: Returns false if calibration is missing, corrupt, or exceeds error limits).
 */
bool touch_hal_is_calibrated(void);

/**
 * Get pointer to active calibration blob.
 */
const TouchCalibrationBlob* touch_hal_get_calibration(void);

/**
 * Set and activate a new calibration blob, saving to NVS.
 */
bool touch_hal_set_calibration(const TouchCalibrationBlob *blob);

/**
 * Set screen rotation (false = Landscape, true = Landscape 180°).
 */
void touch_hal_set_rotation(bool rotation_180);

/**
 * Get current screen rotation setting.
 */
bool touch_hal_get_rotation(void);

/**
 * Mirror / Invert Y axis coordinates.
 */
void touch_hal_set_invert_y(bool invert_y);
bool touch_hal_get_invert_y(void);

/**
 * Mirror / Invert X axis coordinates.
 */
void touch_hal_set_invert_x(bool invert_x);
bool touch_hal_get_invert_x(void);

/**
 * Get the latest filtered, calibrated touch coordinates for LVGL indev.
 * @return true if screen is actively touched, false otherwise.
 */
bool touch_hal_get_point(int16_t *out_x, int16_t *out_y);

/**
 * Get the latest raw reading (for calibration wizard).
 * @return true if touch is detected, false otherwise.
 */
bool touch_hal_get_raw(int16_t *out_raw_x, int16_t *out_raw_y, int16_t *out_raw_z);

/**
 * Query current touch sampling frequency (Hz) for diagnostics.
 */
uint32_t touch_hal_get_sample_hz(void);

/**
 * True when the controller is answering but has never reported a real contact:
 * every press it sends is saturated, which is the signature of an open or
 * shorted touch layer rather than a finger. Latched for the boot; a single
 * valid press clears it.
 *
 * This is a measurement, not a timeout, and it exists so the UI can stop
 * waiting for taps that cannot arrive (measured on 2026-10-21: 70 saturated
 * presses at one fixed coordinate, no contact at all under a finger).
 */
bool touch_hal_is_faulted(void);

/**
 * How many presses were rejected as saturated non-contacts since boot.
 */
uint32_t touch_hal_get_rejected_presses(void);

/**
 * True when the panel reports contact but the same raw coordinate for every
 * press, so position cannot be sensed at all. Measured on 2026-10-21: five taps
 * at five targets 260 px apart all returned raw (0,1327)..(0,1337) - a live
 * PENIRQ with dead X/Y plates. Calibration cannot succeed on such a panel, so
 * the wizard reports the fault instead of asking for more taps.
 */
bool touch_hal_is_position_dead(void);

/**
 * Mark the position axis unusable after a wizard measured it (five taps, one raw
 * point). Latched for the boot; cleared by touch_hal_clear_fault().
 */
void touch_hal_mark_position_dead(void);

/**
 * Enable/disable calibration mode (bypasses filtering/coordinate transforms).
 */
void touch_hal_set_calibration_mode(bool enable);

/**
 * Clear the latched fault (e.g. after the panel or its FPC was reseated).
 */
void touch_hal_clear_fault(void);

/**
 * Save measured 5-point calibration, verifying residual error bounds.
 */
bool touch_hal_save_calibration(const CalibrationPoint points[CALIBRATION_POINT_COUNT], float *out_mean, float *out_max);

/**
 * Clear stored calibration blob from NVS.
 */
void touch_hal_clear_calibration(void);

#ifdef __cplusplus
}

inline bool touch_hal_get_raw(int16_t *out_raw_x, int16_t *out_raw_y) {
    return touch_hal_get_raw(out_raw_x, out_raw_y, nullptr);
}
#endif

#endif // DASHCUE_TOUCH_HAL_H
