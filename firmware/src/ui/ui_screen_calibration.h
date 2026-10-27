#ifndef DASHCUE_UI_SCREEN_CALIBRATION_H
#define DASHCUE_UI_SCREEN_CALIBRATION_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * 5-point touch calibration & alignment verification screen.
 *
 * Draws targets one crosshair at a time across the 4 corners + center.
 * Collects both calibrated coordinates and raw digitizer readings.
 * Maps out residual error across 6 rotation/mirror candidates:
 *   - Normal (0 deg)
 *   - Inverted Y (Mirror Y)
 *   - Inverted X (Mirror X)
 *   - Rotated 180 deg (Flip Both)
 *   - Transposed (Swap XY)
 *   - Transposed + Invert Y
 * Automatically selects and activates the best alignment and saves to NVS!
 */
class UiScreenCalibration {
public:
    UiScreenCalibration();

    void create(lv_obj_t *parent);
    void destroy();
    void show();
    void hide();
    bool isCreated() const { return m_screen != nullptr; }

    /** Periodic poll: advances the crosshair sequence and scores taps. */
    void update();

private:
    static const uint8_t kVerifyPoints = 5;
    static const uint16_t kTolerancePx = 28;

    lv_obj_t *m_screen;
    lv_obj_t *m_instruct_label;
    lv_obj_t *m_result_label;

    // Crosshair graphical elements
    lv_obj_t *m_crosshair_h;
    lv_obj_t *m_crosshair_v;
    lv_obj_t *m_crosshair_dot;

    uint8_t m_current_target; // 0..kVerifyPoints-1
    uint8_t m_pass_count;
    bool m_press_active;  // a press is being collected for the current target
    bool m_was_pressed;   // previous poll saw contact (edge detection)
    bool m_completed;     // sequence finished (pass or fail)
    bool m_pass;          // final result
    uint32_t m_complete_time_ms;
    uint32_t m_retry_arm_ms; // debounce before a retry tap counts

    static constexpr uint32_t kTapSampleCap = 64;
    uint32_t m_sample_count;
    int16_t m_tap_x[kTapSampleCap];
    int16_t m_tap_y[kTapSampleCap];
    int16_t m_raw_x[kTapSampleCap];
    int16_t m_raw_y[kTapSampleCap];

    // Coordinates reported for each target (both calibrated and raw ADC)
    int16_t m_got_x[kVerifyPoints];
    int16_t m_got_y[kVerifyPoints];
    int16_t m_got_raw_x[kVerifyPoints];
    int16_t m_got_raw_y[kVerifyPoints];

    void layoutTarget();
    void resetVerify();
    void recordTap(int16_t got_x, int16_t got_y, int16_t raw_x, int16_t raw_y);
    void finishVerify();
    static int16_t median_of(int16_t *values, uint32_t n);
};

#endif // DASHCUE_UI_SCREEN_CALIBRATION_H
