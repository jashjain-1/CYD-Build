#ifndef DASHCUE_UI_SCREEN_CALIBRATION_H
#define DASHCUE_UI_SCREEN_CALIBRATION_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>
#include "touch/touch_calibration.h"

class UiScreenCalibration {
public:
    UiScreenCalibration();

    void create(lv_obj_t *parent);
    void destroy();
    void show();
    void hide();
    bool isCreated() const { return m_screen != nullptr; }

    /**
     * Periodic poll during calibration screen to capture touches and advance wizard.
     */
    void update();

private:
    lv_obj_t *m_screen;
    lv_obj_t *m_instruct_label;
    lv_obj_t *m_result_label;

    // Crosshair graphical elements
    lv_obj_t *m_crosshair_h;
    lv_obj_t *m_crosshair_v;
    lv_obj_t *m_crosshair_dot;

    uint8_t m_current_target; // 0..4
    CalibrationPoint m_points[TOUCH_CAL_NUM_POINTS];

    // Sample averaging for current target
    int32_t m_sample_sum_x;
    int32_t m_sample_sum_y;
    uint32_t m_sample_count;
    bool m_was_pressed;
    bool m_completed;
    uint32_t m_complete_time_ms;
    uint32_t m_last_touch_ms;
    uint32_t m_last_status_ms;

    void updateTargetPosition();
    void finishCalibration();
    void resetWizard();
};

#endif // DASHCUE_UI_SCREEN_CALIBRATION_H
