#include "ui_screen_calibration.h"
#include "ui_theme.h"
#include "touch/touch_hal.h"
#include "ui/ui_manager.h"
#include <stdio.h>

#ifndef DASHCUE_NATIVE_TEST
#include <Arduino.h>
#else
static uint32_t millis() { return 0; }
#endif

UiScreenCalibration::UiScreenCalibration()
    : m_screen(nullptr),
      m_instruct_label(nullptr),
      m_result_label(nullptr),
      m_crosshair_h(nullptr),
      m_crosshair_v(nullptr),
      m_crosshair_dot(nullptr),
      m_current_target(0),
      m_sample_sum_x(0),
      m_sample_sum_y(0),
      m_sample_count(0),
      m_was_pressed(false),
      m_completed(false),
      m_complete_time_ms(0),
      m_last_touch_ms(0),
      m_last_status_ms(0) {
    memset(m_points, 0, sizeof(m_points));
}

void UiScreenCalibration::create(lv_obj_t *parent) {
    if (m_screen) return;

    m_screen = lv_obj_create(parent);
    lv_obj_set_size(m_screen, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(m_screen, 0, 0);
    lv_obj_set_style_bg_color(m_screen, lv_color_hex(0x050811), 0);
    lv_obj_set_style_bg_opa(m_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(m_screen, 0, 0);
    lv_obj_set_style_radius(m_screen, 0, 0);
    lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Instruction banner at center-top
    m_instruct_label = lv_label_create(m_screen);
    lv_obj_set_width(m_instruct_label, 300);
    lv_obj_set_style_text_align(m_instruct_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(m_instruct_label, "Touch Calibration: Tap crosshair (1/5)");
    lv_obj_set_style_text_color(m_instruct_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_instruct_label, UI_FONT_MD, 0);
    lv_obj_align(m_instruct_label, LV_ALIGN_TOP_MID, 0, 8);

    // Result banner at center-bottom
    m_result_label = lv_label_create(m_screen);
    lv_obj_set_width(m_result_label, 300);
    lv_obj_set_style_text_align(m_result_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(m_result_label, "Press the crosshair, hold briefly, then lift.");
    lv_obj_add_style(m_result_label, &ui_style_text_dim_sm, 0);
    lv_obj_align(m_result_label, LV_ALIGN_CENTER, 0, 48);

    // Crosshair horizontal line (24x2 px)
    m_crosshair_h = lv_obj_create(m_screen);
    lv_obj_set_size(m_crosshair_h, 24, 2);
    lv_obj_set_style_bg_color(m_crosshair_h, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(m_crosshair_h, 0, 0);
    lv_obj_set_style_radius(m_crosshair_h, 0, 0);

    // Crosshair vertical line (2x24 px)
    m_crosshair_v = lv_obj_create(m_screen);
    lv_obj_set_size(m_crosshair_v, 2, 24);
    lv_obj_set_style_bg_color(m_crosshair_v, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(m_crosshair_v, 0, 0);
    lv_obj_set_style_radius(m_crosshair_v, 0, 0);

    // Crosshair center dot (4x4 px)
    m_crosshair_dot = lv_obj_create(m_screen);
    lv_obj_set_size(m_crosshair_dot, 6, 6);
    lv_obj_set_style_bg_color(m_crosshair_dot, UI_COLOR_OK, 0);
    lv_obj_set_style_border_width(m_crosshair_dot, 0, 0);
    lv_obj_set_style_radius(m_crosshair_dot, 3, 0);

    lv_obj_add_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenCalibration::destroy() {
    if (m_screen) {
        touch_hal_set_calibration_mode(false);
        lv_obj_delete(m_screen);
        m_screen = nullptr;
        m_instruct_label = nullptr;
        m_result_label = nullptr;
        m_crosshair_h = nullptr;
        m_crosshair_v = nullptr;
        m_crosshair_dot = nullptr;
    }
}

void UiScreenCalibration::show() {
    if (!m_screen) return;
    lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
    touch_hal_set_calibration_mode(true);
    resetWizard();
}

void UiScreenCalibration::hide() {
    if (!m_screen) return;
    lv_obj_add_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
    touch_hal_set_calibration_mode(false);
}

void UiScreenCalibration::resetWizard() {
    m_current_target = 0;
    m_sample_sum_x = 0;
    m_sample_sum_y = 0;
    m_sample_count = 0;
    m_was_pressed = false;
    m_completed = false;
    m_complete_time_ms = 0;
    m_last_touch_ms = millis();
    m_last_status_ms = 0;
    memset(m_points, 0, sizeof(m_points));
    if (m_screen) {
        updateTargetPosition();
    }
}

void UiScreenCalibration::updateTargetPosition() {
    if (!m_screen || m_current_target >= TOUCH_CAL_NUM_POINTS) return;

    int16_t sx = 0, sy = 0;
    TouchCalibration::getTargetCoordinate(m_current_target, sx, sy);

    lv_obj_set_pos(m_crosshair_h, sx - 12, sy - 1);
    lv_obj_set_pos(m_crosshair_v, sx - 1, sy - 12);
    lv_obj_set_pos(m_crosshair_dot, sx - 3, sy - 3);

    char buf[64];
    snprintf(buf, sizeof(buf), "Touch Calibration: Tap crosshair (%u/5)", (unsigned)(m_current_target + 1));
    lv_label_set_text(m_instruct_label, buf);
}

void UiScreenCalibration::update() {
    if (!m_screen) return;

    if (m_completed) {
        if (millis() - m_complete_time_ms >= 1500) {
            UiManager::instance().navigateTo(UI_SCREEN_MEDIA);
        }
        return;
    }

    const uint32_t now_ms = millis();
    int16_t raw_x = 0, raw_y = 0, raw_z = 0;
    bool pressed = touch_hal_get_raw(&raw_x, &raw_y, &raw_z);

    if (pressed) {
        m_last_touch_ms = now_ms;
        // Bounded accumulation even if a stylus is left pressed indefinitely.
        if (m_sample_count < 1000) {
            m_sample_sum_x += raw_x;
            m_sample_sum_y += raw_y;
            m_sample_count++;
        }
        m_was_pressed = true;
        if (now_ms - m_last_status_ms >= 150) {
            m_last_status_ms = now_ms;
            char buf[72];
            snprintf(buf, sizeof(buf), "Touch detected: %d, %d (pressure %d). Lift to accept.",
                     raw_x, raw_y, raw_z);
            lv_label_set_text(m_result_label, buf);
        }

        // Visual feedback: turn dot accent color while touched
        lv_obj_set_style_bg_color(m_crosshair_dot, UI_COLOR_WARN, 0);
    } else {
        lv_obj_set_style_bg_color(m_crosshair_dot, UI_COLOR_OK, 0);

        if (m_was_pressed && m_sample_count < 2) {
            // Discard a short contact rather than combining separate taps.
            m_sample_sum_x = 0;
            m_sample_sum_y = 0;
            m_sample_count = 0;
            m_was_pressed = false;
            lv_label_set_text(m_result_label, "Tap too short. Hold the crosshair briefly, then lift.");
        } else if (m_was_pressed) {
            // Register averaged touch point
            int16_t avg_x = (int16_t)(m_sample_sum_x / (int32_t)m_sample_count);
            int16_t avg_y = (int16_t)(m_sample_sum_y / (int32_t)m_sample_count);

            int16_t sx = 0, sy = 0;
            TouchCalibration::getTargetCoordinate(m_current_target, sx, sy);

            m_points[m_current_target].screen_x = sx;
            m_points[m_current_target].screen_y = sy;
            m_points[m_current_target].raw_x = avg_x;
            m_points[m_current_target].raw_y = avg_y;
#ifndef DASHCUE_NATIVE_TEST
            Serial.printf("CAL target=%u raw=(%d,%d) screen=(%d,%d) samples=%u\n",
                          (unsigned)m_current_target + 1, avg_x, avg_y, sx, sy,
                          (unsigned)m_sample_count);
#endif

            m_sample_sum_x = 0;
            m_sample_sum_y = 0;
            m_sample_count = 0;
            m_was_pressed = false;

            m_current_target++;
            if (m_current_target >= TOUCH_CAL_NUM_POINTS) {
                finishCalibration();
            } else {
                updateTargetPosition();
                lv_label_set_text(m_result_label, "Point accepted. Press the next crosshair, then lift.");
            }
        } else if (now_ms - m_last_touch_ms >= 10000 && now_ms - m_last_status_ms >= 1000) {
            m_last_status_ms = now_ms;
            lv_label_set_text(m_result_label, "No touch detected. Press firmly with the stylus.");
        }
    }
}

void UiScreenCalibration::finishCalibration() {
    float mean_res = 0.0f;
    float max_res = 0.0f;

    bool pass = touch_hal_save_calibration(m_points, &mean_res, &max_res);
#ifndef DASHCUE_NATIVE_TEST
    Serial.printf("CAL result=%s mean_px=%.2f max_px=%.2f\n", pass ? "PASS" : "FAIL", mean_res, max_res);
#endif

    if (pass) {
        char buf[80];
        snprintf(buf, sizeof(buf), "CALIBRATION PASS\nMean: %.1f px (<=6), Max: %.1f px (<=12)",
                 mean_res, max_res);
        lv_label_set_text(m_instruct_label, buf);
        lv_obj_set_style_text_color(m_instruct_label, UI_COLOR_OK, 0);
        lv_label_set_text(m_result_label, "Calibration saved to NVS. Loading HUD...");
        m_completed = true;
        m_complete_time_ms = millis();
        touch_hal_set_calibration_mode(false);
    } else {
        char buf[80];
        snprintf(buf, sizeof(buf), "CALIBRATION FAILED\nResiduals: Mean %.1f px, Max %.1f px",
                 mean_res, max_res);
        lv_label_set_text(m_instruct_label, buf);
        lv_obj_set_style_text_color(m_instruct_label, UI_COLOR_DANGER, 0);
        lv_label_set_text(m_result_label, "Residual limits exceeded. Restarting wizard...");
        resetWizard();
    }
}
