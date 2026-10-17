#ifndef DASHCUE_TOUCH_CALIBRATION_H
#define DASHCUE_TOUCH_CALIBRATION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOUCH_CALIBRATION_MAGIC 0x4443414Cu // "DCAL"
#define TOUCH_CALIBRATION_VERSION 1u

#define CALIBRATION_POINT_COUNT 5

#define TOUCH_CAL_MAX_MEAN_RESIDUAL_PX 6.0f  // Rule / Spec bound
#define TOUCH_CAL_MAX_PEAK_RESIDUAL_PX 12.0f // Rule / Spec bound

typedef struct {
    int16_t x;
    int16_t y;
    bool pressed;
} TouchPoint;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} TouchRawPoint;

typedef struct {
    int16_t screen_x;
    int16_t screen_y;
    int16_t raw_x;
    int16_t raw_y;
} CalibrationPoint;

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;           // 0x4443414C ("DCAL")
    uint16_t version;         // 1
    union {
        uint16_t reserved;    // 0
        uint16_t flags;       // 1 = valid/calibrated
    };
    float alpha_x;            // Matrix coefficient A
    float beta_x;             // Matrix coefficient B
    float delta_x;            // Matrix coefficient C
    float alpha_y;            // Matrix coefficient D
    float beta_y;             // Matrix coefficient E
    float delta_y;            // Matrix coefficient F
    float residual_mean_px;   // Mean residual error across 5 points (must be <= 6.0)
    float residual_max_px;    // Max residual error across 5 points (must be <= 12.0)
    uint32_t calibrated_at_s; // Monotonic uptime timestamp in seconds
    uint32_t crc32;           // IEEE 802.3 CRC32 over all preceding bytes
} TouchCalibrationBlob;
#pragma pack(pop)

/**
 * Standard reference calibration targets in landscape pixels (320x240):
 * Point 0: (30, 30)   - Top-Left
 * Point 1: (290, 30)  - Top-Right
 * Point 2: (160, 120) - Center
 * Point 3: (30, 210)  - Bottom-Left
 * Point 4: (290, 210) - Bottom-Right
 */
extern const TouchPoint g_calibration_targets[CALIBRATION_POINT_COUNT];

/**
 * Solve 5-point affine transformation matrix using normal equations.
 * Computes alpha_x, beta_x, delta_x and alpha_y, beta_y, delta_y.
 * Calculates residual errors and checks bounds (mean <= 6 px, max <= 12 px).
 * Fills out_blob with computed parameters and IEEE CRC32.
 *
 * @param raw_points Array of 5 measured raw touch readings.
 * @param screen_points Array of 5 expected screen target coordinates.
 * @param out_blob Pointer to TouchCalibrationBlob to receive results.
 * @return true if matrix is non-singular and residuals meet bounds, false otherwise.
 */
bool touch_calibration_solve(const TouchRawPoint raw_points[CALIBRATION_POINT_COUNT],
                             const TouchPoint screen_points[CALIBRATION_POINT_COUNT],
                             TouchCalibrationBlob *out_blob);

/**
 * Validate calibration blob integrity:
 * - Magic matches 0x4443414C
 * - Version matches 1
 * - CRC32 matches computed IEEE CRC32 over preceding bytes
 * - residual_mean_px <= 6.0f
 * - residual_max_px <= 12.0f
 */
bool touch_calibration_is_valid(const TouchCalibrationBlob *blob);

/**
 * Apply 5-point affine transformation to raw touch coordinates.
 * Clamps output to display boundaries [0..width-1] and [0..height-1].
 * Handles 180° rotation if requested.
 */
TouchPoint touch_calibration_apply(const TouchCalibrationBlob *blob,
                                   int16_t raw_x,
                                   int16_t raw_y,
                                   bool rotation_180);

/**
 * Load calibration blob from non-volatile storage (NVS / Preferences).
 * @return true if valid blob loaded, false if missing or corrupted.
 */
bool touch_calibration_load_nvs(TouchCalibrationBlob *out_blob);

/**
 * Save calibration blob to non-volatile storage.
 * @return true if successfully persisted.
 */
bool touch_calibration_save_nvs(const TouchCalibrationBlob *blob);

/**
 * Erase calibration blob from non-volatile storage.
 */
void touch_calibration_erase_nvs(void);

/**
 * Get factory default calibration blob for CYD panel.
 */
bool touch_calibration_get_default(TouchCalibrationBlob *out_blob);

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_TOUCH_CALIBRATION_H
