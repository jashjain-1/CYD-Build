#ifndef DASHCUE_TOUCH_HAL_H
#define DASHCUE_TOUCH_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "calibration.h"

#ifdef __cplusplus
extern "C" {
#endif

// 2,048 B: measured high-water 556 B on 2026-10-20 (was 4,096). Kept at ~3.7x
// because this task calls into the SPI touch HAL, where a driver call can spike.
#define TOUCH_TASK_STACK_SIZE 2048u
#define TOUCH_TASK_PRIORITY   5u
#define TOUCH_TASK_CORE_ID    1u

/**
 * Initialize dedicated HSPI bus for touch controller (pins 25/32/39/33),
 * attempt to load NVS calibration blob, verify CRC32 and residual bounds.
 */
void touch_hal_setup(void);

/**
 * Start the 100 Hz touch sampling task on Core 1 (priority 4).
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
 * Enable/disable calibration mode (bypasses filtering/coordinate transforms).
 */
void touch_hal_set_calibration_mode(bool enable);

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
