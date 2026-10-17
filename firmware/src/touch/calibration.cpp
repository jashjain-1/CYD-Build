#include "calibration.h"
#include "dashcue_config.h"
#include "protocol/crc32.h"

#include <math.h>
#include <string.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <Arduino.h>
#include <Preferences.h>
#endif

const TouchPoint g_calibration_targets[CALIBRATION_POINT_COUNT] = {
    {30, 30, true},    // Point 0: Top-Left
    {290, 30, true},   // Point 1: Top-Right
    {160, 120, true},  // Point 2: Center
    {30, 210, true},   // Point 3: Bottom-Left
    {290, 210, true}   // Point 4: Bottom-Right
};

bool touch_calibration_solve(const TouchRawPoint raw_points[CALIBRATION_POINT_COUNT],
                             const TouchPoint screen_points[CALIBRATION_POINT_COUNT],
                             TouchCalibrationBlob *out_blob) {
    if (!raw_points || !screen_points || !out_blob) {
        return false;
    }

    // Accumulate sums for normal equations (M^T * M) * theta = M^T * Y
    // Matrix M row i: [X_ri, Y_ri, 1.0]
    double s_xx = 0.0, s_yy = 0.0, s_xy = 0.0;
    double s_x = 0.0, s_y = 0.0;
    const double N = (double)CALIBRATION_POINT_COUNT;

    double vx_x = 0.0, vx_y = 0.0, vx_c = 0.0;
    double vy_x = 0.0, vy_y = 0.0, vy_c = 0.0;

    for (int i = 0; i < CALIBRATION_POINT_COUNT; i++) {
        double xr = (double)raw_points[i].x;
        double yr = (double)raw_points[i].y;
        double xs = (double)screen_points[i].x;
        double ys = (double)screen_points[i].y;

        s_xx += xr * xr;
        s_yy += yr * yr;
        s_xy += xr * yr;
        s_x  += xr;
        s_y  += yr;

        vx_x += xr * xs;
        vx_y += yr * xs;
        vx_c += xs;

        vy_x += xr * ys;
        vy_y += yr * ys;
        vy_c += ys;
    }

    // G = M^T * M (3x3 symmetric matrix)
    // [ a00  a01  a02 ]   [ s_xx  s_xy  s_x ]
    // [ a10  a11  a12 ] = [ s_xy  s_yy  s_y ]
    // [ a20  a21  a22 ]   [ s_x   s_y   N   ]
    double a00 = s_xx, a01 = s_xy, a02 = s_x;
    double a10 = s_xy, a11 = s_yy, a12 = s_y;
    double a20 = s_x,  a21 = s_y,  a22 = N;

    // Determinant of G
    double det = a00 * (a11 * a22 - a12 * a21) -
                 a01 * (a10 * a22 - a12 * a20) +
                 a02 * (a10 * a21 - a11 * a20);

    // Reject singular or nearly singular matrix (degenerate points / collinear reads)
    if (fabs(det) < 1e-7) {
        return false;
    }

    double inv_det = 1.0 / det;

    // Adjugate matrix elements divided by det
    double inv00 = (a11 * a22 - a12 * a21) * inv_det;
    double inv01 = (a02 * a21 - a01 * a22) * inv_det;
    double inv02 = (a01 * a12 - a02 * a11) * inv_det;

    double inv10 = (a12 * a20 - a10 * a22) * inv_det;
    double inv11 = (a00 * a22 - a02 * a20) * inv_det;
    double inv12 = (a02 * a10 - a00 * a12) * inv_det;

    double inv20 = (a10 * a21 - a11 * a20) * inv_det;
    double inv21 = (a01 * a20 - a00 * a21) * inv_det;
    double inv22 = (a00 * a11 - a01 * a10) * inv_det;

    // Solve for X coefficients: alpha_x, beta_x, delta_x
    float alpha_x = (float)(inv00 * vx_x + inv01 * vx_y + inv02 * vx_c);
    float beta_x  = (float)(inv10 * vx_x + inv11 * vx_y + inv12 * vx_c);
    float delta_x = (float)(inv20 * vx_x + inv21 * vx_y + inv22 * vx_c);

    // Solve for Y coefficients: alpha_y, beta_y, delta_y
    float alpha_y = (float)(inv00 * vy_x + inv01 * vy_y + inv02 * vy_c);
    float beta_y  = (float)(inv10 * vy_x + inv11 * vy_y + inv12 * vy_c);
    float delta_y = (float)(inv20 * vy_x + inv21 * vy_y + inv22 * vy_c);

    // Calculate residuals for each of the 5 points
    float residual_sum = 0.0f;
    float max_residual = 0.0f;

    for (int i = 0; i < CALIBRATION_POINT_COUNT; i++) {
        float xr = (float)raw_points[i].x;
        float yr = (float)raw_points[i].y;
        float est_x = alpha_x * xr + beta_x * yr + delta_x;
        float est_y = alpha_y * xr + beta_y * yr + delta_y;

        float dx = est_x - (float)screen_points[i].x;
        float dy = est_y - (float)screen_points[i].y;
        float dist = sqrtf(dx * dx + dy * dy);

        residual_sum += dist;
        if (dist > max_residual) {
            max_residual = dist;
        }
    }

    float mean_residual = residual_sum / (float)CALIBRATION_POINT_COUNT;

    // Strict bound check: mean <= 6.0 px, max <= 12.0 px
    if (mean_residual > TOUCH_CAL_MAX_MEAN_RESIDUAL_PX ||
        max_residual > TOUCH_CAL_MAX_PEAK_RESIDUAL_PX) {
        return false;
    }

    // Populate blob
    memset(out_blob, 0, sizeof(TouchCalibrationBlob));
    out_blob->magic = TOUCH_CALIBRATION_MAGIC;
    out_blob->version = TOUCH_CALIBRATION_VERSION;
    out_blob->reserved = 0;
    out_blob->alpha_x = alpha_x;
    out_blob->beta_x = beta_x;
    out_blob->delta_x = delta_x;
    out_blob->alpha_y = alpha_y;
    out_blob->beta_y = beta_y;
    out_blob->delta_y = delta_y;
    out_blob->residual_mean_px = mean_residual;
    out_blob->residual_max_px = max_residual;

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    out_blob->calibrated_at_s = (uint32_t)(millis() / 1000);
#else
    out_blob->calibrated_at_s = 100;
#endif

    // Compute IEEE 802.3 CRC32 over all bytes preceding the crc32 field
    size_t crc_len = offsetof(TouchCalibrationBlob, crc32);
    out_blob->crc32 = crc32_calculate((const uint8_t *)out_blob, crc_len);

    return true;
}

bool touch_calibration_is_valid(const TouchCalibrationBlob *blob) {
    if (!blob) {
        return false;
    }
    if (blob->magic != TOUCH_CALIBRATION_MAGIC) {
        return false;
    }
    if (blob->version != TOUCH_CALIBRATION_VERSION) {
        return false;
    }
    if (blob->residual_mean_px > TOUCH_CAL_MAX_MEAN_RESIDUAL_PX ||
        blob->residual_max_px > TOUCH_CAL_MAX_PEAK_RESIDUAL_PX ||
        blob->residual_mean_px < 0.0f || blob->residual_max_px < 0.0f) {
        return false;
    }

    size_t crc_len = offsetof(TouchCalibrationBlob, crc32);
    uint32_t expected_crc = crc32_calculate((const uint8_t *)blob, crc_len);
    return (blob->crc32 == expected_crc);
}

TouchPoint touch_calibration_apply(const TouchCalibrationBlob *blob,
                                   int16_t raw_x,
                                   int16_t raw_y,
                                   bool rotation_180) {
    TouchPoint pt = {0, 0, false};
    if (!blob) {
        return pt;
    }

    float fx = blob->alpha_x * (float)raw_x + blob->beta_x * (float)raw_y + blob->delta_x;
    float fy = blob->alpha_y * (float)raw_x + blob->beta_y * (float)raw_y + blob->delta_y;

    int32_t ix = (int32_t)roundf(fx);
    int32_t iy = (int32_t)roundf(fy);

    // Clamp within 320x240 screen boundary
    if (ix < 0) ix = 0;
    if (ix >= DISPLAY_WIDTH) ix = DISPLAY_WIDTH - 1;
    if (iy < 0) iy = 0;
    if (iy >= DISPLAY_HEIGHT) iy = DISPLAY_HEIGHT - 1;

    // If landscape 180 is active, invert coordinates
    if (rotation_180) {
        ix = (DISPLAY_WIDTH - 1) - ix;
        iy = (DISPLAY_HEIGHT - 1) - iy;
    }

    pt.x = (int16_t)ix;
    pt.y = (int16_t)iy;
    pt.pressed = true;
    return pt;
}

#if defined(ESP_PLATFORM) || defined(ARDUINO)

static const char *PREFS_NAMESPACE = "dashcue";
static const char *PREFS_KEY = "touch_cal";

bool touch_calibration_load_nvs(TouchCalibrationBlob *out_blob) {
    if (!out_blob) return false;

    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, true)) {
        return false;
    }

    size_t len = prefs.getBytes(PREFS_KEY, out_blob, sizeof(TouchCalibrationBlob));
    prefs.end();

    if (len != sizeof(TouchCalibrationBlob)) {
        return false;
    }

    return touch_calibration_is_valid(out_blob);
}

bool touch_calibration_save_nvs(const TouchCalibrationBlob *blob) {
    if (!blob || !touch_calibration_is_valid(blob)) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }

    size_t written = prefs.putBytes(PREFS_KEY, blob, sizeof(TouchCalibrationBlob));
    prefs.end();

    return (written == sizeof(TouchCalibrationBlob));
}

void touch_calibration_erase_nvs(void) {
    Preferences prefs;
    if (prefs.begin(PREFS_NAMESPACE, false)) {
        prefs.remove(PREFS_KEY);
        prefs.end();
    }
}

#else

static TouchCalibrationBlob s_mock_blob;
static bool s_mock_has_blob = false;

bool touch_calibration_load_nvs(TouchCalibrationBlob *out_blob) {
    if (s_mock_has_blob && touch_calibration_is_valid(&s_mock_blob)) {
        *out_blob = s_mock_blob;
        return true;
    }
    return false;
}

bool touch_calibration_save_nvs(const TouchCalibrationBlob *blob) {
    if (!blob || !touch_calibration_is_valid(blob)) return false;
    s_mock_blob = *blob;
    s_mock_has_blob = true;
    return true;
}

void touch_calibration_erase_nvs(void) {
    s_mock_has_blob = false;
}

#endif

bool touch_calibration_get_default(TouchCalibrationBlob *out_blob) {
    if (!out_blob) return false;
    memset(out_blob, 0, sizeof(TouchCalibrationBlob));
    out_blob->magic = TOUCH_CALIBRATION_MAGIC;
    out_blob->version = TOUCH_CALIBRATION_VERSION;
    out_blob->flags = 1;
    out_blob->alpha_x = 0.07647059f;
    out_blob->beta_x = 0.0f;
    out_blob->delta_x = 7.0588235f;
    out_blob->alpha_y = 0.0f;
    out_blob->beta_y = -0.06428571f;
    out_blob->delta_y = 248.57143f;
    out_blob->residual_mean_px = 1.5f;
    out_blob->residual_max_px = 3.0f;
    out_blob->calibrated_at_s = 100;
    size_t crc_len = offsetof(TouchCalibrationBlob, crc32);
    out_blob->crc32 = crc32_calculate((const uint8_t *)out_blob, crc_len);
    return true;
}
