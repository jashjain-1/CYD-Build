#include "ui_screen_calibration.h"
#include "ui_theme.h"
#include "dashcue_config.h"
#include "touch/touch_hal.h"
#include "touch/calibration.h"
#include "display/display_hal.h"
#include "ui/ui_manager.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef DASHCUE_NATIVE_TEST
#include <Arduino.h>
#else
static uint32_t millis() { return 0; }
#endif

namespace {

// 5 Canonical Verification & Calibration targets:
// Four corners (inset 40 px) + screen Center (240, 160).
// Inset 40 px ensures taps remain clear of bezel deflection.
struct VerifyTarget { int16_t x; int16_t y; const char *name; };
constexpr uint8_t kTargetCount = 5;
const VerifyTarget kTargets[kTargetCount] = {
    { 40,  40, "Top-Left"},
    {440,  40, "Top-Right"},
    {240, 160, "Center"},
    { 40, 280, "Bottom-Left"},
    {440, 280, "Bottom-Right"}
};

} // namespace

UiScreenCalibration::UiScreenCalibration()
    : m_screen(nullptr),
      m_instruct_label(nullptr),
      m_result_label(nullptr),
      m_crosshair_h(nullptr),
      m_crosshair_v(nullptr),
      m_crosshair_dot(nullptr),
      m_current_target(0),
      m_pass_count(0),
      m_press_active(false),
      m_was_pressed(false),
      m_completed(false),
      m_pass(false),
      m_complete_time_ms(0),
      m_retry_arm_ms(0),
      m_sample_count(0),
      m_tap_x{},
      m_tap_y{},
      m_raw_x{},
      m_raw_y{},
      m_got_x{},
      m_got_y{},
      m_got_raw_x{},
      m_got_raw_y{} {}

int16_t UiScreenCalibration::median_of(int16_t *values, uint32_t n) {
    for (uint32_t i = 1; i < n; ++i) {
        const int16_t key = values[i];
        uint32_t j = i;
        while (j > 0 && values[j - 1] > key) {
            values[j] = values[j - 1];
            --j;
        }
        values[j] = key;
    }
    return values[n / 2];
}

void UiScreenCalibration::create(lv_obj_t *parent) {
    if (m_screen) return;

    m_screen = lv_obj_create(parent);
    lv_obj_set_size(m_screen, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(m_screen, 0, 0);
    lv_obj_set_style_bg_color(m_screen, lv_color_hex(0x050608), 0);
    lv_obj_set_style_bg_opa(m_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(m_screen, 0, 0);
    lv_obj_set_style_radius(m_screen, 0, 0);
    lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Instruction banner at top
    m_instruct_label = lv_label_create(m_screen);
    lv_obj_set_width(m_instruct_label, 460);
    lv_obj_set_style_text_align(m_instruct_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(m_instruct_label, "Touch Calibration: tap each crosshair (1/5)");
    lv_obj_set_style_text_color(m_instruct_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_instruct_label, UI_FONT_MD, 0);
    lv_obj_align(m_instruct_label, LV_ALIGN_TOP_MID, 0, 8);

    // Result banner at bottom
    m_result_label = lv_label_create(m_screen);
    lv_obj_set_width(m_result_label, 460);
    lv_obj_set_style_text_align(m_result_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(m_result_label, "Tap each crosshair once.\nTolerance: +/-28 px");
    lv_obj_set_style_text_color(m_result_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(m_result_label, UI_FONT_MD, 0);
    lv_obj_align(m_result_label, LV_ALIGN_BOTTOM_MID, 0, 12);

    // Crosshairs: 32x2 px horizontal, 2x32 px vertical, 10x10 px center dot
    m_crosshair_h = lv_obj_create(m_screen);
    lv_obj_set_size(m_crosshair_h, 32, 2);
    lv_obj_set_style_bg_color(m_crosshair_h, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(m_crosshair_h, 0, 0);
    lv_obj_set_style_radius(m_crosshair_h, 0, 0);
    lv_obj_remove_flag(m_crosshair_h, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(m_crosshair_h, LV_OBJ_FLAG_CLICKABLE);

    m_crosshair_v = lv_obj_create(m_screen);
    lv_obj_set_size(m_crosshair_v, 2, 32);
    lv_obj_set_style_bg_color(m_crosshair_v, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(m_crosshair_v, 0, 0);
    lv_obj_set_style_radius(m_crosshair_v, 0, 0);
    lv_obj_remove_flag(m_crosshair_v, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(m_crosshair_v, LV_OBJ_FLAG_CLICKABLE);

    m_crosshair_dot = lv_obj_create(m_screen);
    lv_obj_set_size(m_crosshair_dot, 10, 10);
    lv_obj_set_style_bg_color(m_crosshair_dot, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(m_crosshair_dot, 0, 0);
    lv_obj_set_style_radius(m_crosshair_dot, 5, 0);
    lv_obj_remove_flag(m_crosshair_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(m_crosshair_dot, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenCalibration::destroy() {
    if (!m_screen) return;
    lv_obj_delete(m_screen);
    m_screen = nullptr;
    m_instruct_label = nullptr;
    m_result_label = nullptr;
    m_crosshair_h = nullptr;
    m_crosshair_v = nullptr;
    m_crosshair_dot = nullptr;
}

void UiScreenCalibration::show() {
    if (!m_screen) return;
    lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_HIDDEN);

    if (!touch_hal_is_calibrated()) {
        TouchCalibrationBlob blob;
        if (touch_calibration_get_default(&blob) && touch_hal_set_calibration(&blob)) {
            Serial.println("VERIFY: panel uncalibrated - loaded factory default matrix");
        } else {
            Serial.println("VERIFY: panel uncalibrated and factory matrix rejected");
        }
    }
#ifndef DASHCUE_NATIVE_TEST
    Serial.printf("VERIFY: start 5-point touch calibration & error mapping (tol=%u px)\n", kTolerancePx);
#endif
    resetVerify();
}

void UiScreenCalibration::hide() {
    if (!m_screen) return;
    lv_obj_add_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenCalibration::resetVerify() {
    m_current_target = 0;
    m_pass_count = 0;
    m_press_active = false;
    m_was_pressed = false;
    m_completed = false;
    m_pass = false;
    m_sample_count = 0;
    memset(m_got_x, 0, sizeof(m_got_x));
    memset(m_got_y, 0, sizeof(m_got_y));
    memset(m_got_raw_x, 0, sizeof(m_got_raw_x));
    memset(m_got_raw_y, 0, sizeof(m_got_raw_y));
    if (!m_screen) return;
    lv_label_set_text(m_result_label, "Tap each crosshair once.\nTolerance: +/-28 px");
    lv_obj_set_style_text_color(m_result_label, UI_COLOR_TEXT_DIM, 0);
    layoutTarget();
}

void UiScreenCalibration::layoutTarget() {
    if (!m_screen || m_current_target >= kTargetCount) return;
    const VerifyTarget &t = kTargets[m_current_target];
    lv_obj_set_pos(m_crosshair_h, t.x - 16, t.y - 1);
    lv_obj_set_pos(m_crosshair_v, t.x - 1, t.y - 16);
    lv_obj_set_pos(m_crosshair_dot, t.x - 5, t.y - 5);
    lv_obj_remove_flag(m_crosshair_h, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(m_crosshair_v, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(m_crosshair_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(m_crosshair_dot, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_color(m_crosshair_h, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_color(m_crosshair_v, UI_COLOR_ACCENT, 0);

    char buf[64];
    snprintf(buf, sizeof(buf), "Touch Crosshair %u of %u (%s)",
             (unsigned)(m_current_target + 1), (unsigned)kTargetCount, t.name);
    lv_label_set_text(m_instruct_label, buf);
    lv_obj_set_style_text_color(m_instruct_label, UI_COLOR_TEXT, 0);
}

void UiScreenCalibration::update() {
    if (!m_screen) return;

    int16_t gx = 0, gy = 0;
    const bool pressed = touch_hal_get_point(&gx, &gy);
    int16_t rx = 0, ry = 0, rz = 0;
    const bool has_raw = touch_hal_get_raw(&rx, &ry, &rz);

    if (m_completed) {
        if (m_pass && (millis() - m_complete_time_ms) > 3500) {
            UiManager::instance().navigateTo(UI_SCREEN_MEDIA);
            return;
        }
        if (pressed && !m_was_pressed && (millis() - m_retry_arm_ms) > 500) {
            if (m_pass) {
                UiManager::instance().navigateTo(UI_SCREEN_MEDIA);
            } else {
                resetVerify();
            }
            m_was_pressed = pressed;
            return;
        }
        m_was_pressed = pressed;
        return;
    }

    if (pressed) {
        if (!m_press_active) {
            m_press_active = true;
            m_sample_count = 0;
            // Visual touch feedback: turns green on contact!
            if (m_crosshair_dot) lv_obj_set_style_bg_color(m_crosshair_dot, UI_COLOR_OK, 0);
            if (m_crosshair_h) lv_obj_set_style_bg_color(m_crosshair_h, UI_COLOR_OK, 0);
            if (m_crosshair_v) lv_obj_set_style_bg_color(m_crosshair_v, UI_COLOR_OK, 0);
        }
        if (m_sample_count < kTapSampleCap) {
            m_tap_x[m_sample_count] = gx;
            m_tap_y[m_sample_count] = gy;
            m_raw_x[m_sample_count] = has_raw ? rx : 0;
            m_raw_y[m_sample_count] = has_raw ? ry : 0;
            m_sample_count++;
        }
    } else if (m_press_active) {
        m_press_active = false;
        if (m_crosshair_dot) lv_obj_set_style_bg_color(m_crosshair_dot, UI_COLOR_ACCENT, 0);
        if (m_crosshair_h) lv_obj_set_style_bg_color(m_crosshair_h, UI_COLOR_ACCENT, 0);
        if (m_crosshair_v) lv_obj_set_style_bg_color(m_crosshair_v, UI_COLOR_ACCENT, 0);

        if (m_sample_count >= 2) {
            recordTap(median_of(m_tap_x, m_sample_count),
                      median_of(m_tap_y, m_sample_count),
                      median_of(m_raw_x, m_sample_count),
                      median_of(m_raw_y, m_sample_count));
        } else {
            lv_label_set_text(m_result_label, "Tap too short - hold briefly, then release");
        }
    }
    m_was_pressed = pressed;
}

void UiScreenCalibration::recordTap(int16_t got_x, int16_t got_y, int16_t raw_x, int16_t raw_y) {
    const VerifyTarget &t = kTargets[m_current_target];
    m_got_x[m_current_target] = got_x;
    m_got_y[m_current_target] = got_y;
    m_got_raw_x[m_current_target] = raw_x;
    m_got_raw_y[m_current_target] = raw_y;

    const int dx = (int)got_x - t.x;
    const int dy = (int)got_y - t.y;
    const int tol2 = (int)kTolerancePx * kTolerancePx;
    const bool ok = (dx * dx + dy * dy) <= tol2;
    if (ok) m_pass_count++;

#ifndef DASHCUE_NATIVE_TEST
    Serial.printf("VERIFY: %u/%u %-12s exp=(%3d,%3d) got=(%3d,%3d) raw=(%4d,%4d) dx=%d dy=%d %s\n",
                  (unsigned)(m_current_target + 1), (unsigned)kTargetCount, t.name,
                  t.x, t.y, got_x, got_y, raw_x, raw_y,
                  dx, dy, ok ? "PASS" : "FAIL");
#endif

    char buf[128];
    snprintf(buf, sizeof(buf), "Target %u/%u (%s): dx=%d dy=%d\nTap next crosshair...",
             (unsigned)(m_current_target + 1), (unsigned)kTargetCount, t.name, dx, dy);
    lv_label_set_text(m_result_label, buf);
    lv_obj_set_style_text_color(m_result_label, ok ? UI_COLOR_OK : UI_COLOR_ACCENT, 0);

    if (m_current_target + 1 < kTargetCount) {
        m_current_target++;
        layoutTarget();
    } else {
        finishVerify();
    }
}

void UiScreenCalibration::finishVerify() {
    m_completed = true;
    m_complete_time_ms = millis();
    m_retry_arm_ms = millis();

    // Map out errors across all candidate transformations:
    // Candidate 0: Normal (0 deg)
    // Candidate 1: Inverted Y (Flip Y)
    // Candidate 2: Inverted X (Flip X)
    // Candidate 3: Rotated 180 deg
    // Candidate 4: Transposed (Swap XY)
    // Candidate 5: Transposed + Invert Y
    struct Candidate {
        const char *name;
        float mean_err_px;
        float max_err_px;
        uint8_t pass_count;
        int action_id; // 0=normal, 1=invert_y, 2=invert_x, 3=rot180
    };

    Candidate cands[6] = {
        {"Normal (0 deg)", 0.0f, 0.0f, 0, 0},
        {"Inverted Y (Flip Y)", 0.0f, 0.0f, 0, 1},
        {"Inverted X (Flip X)", 0.0f, 0.0f, 0, 2},
        {"Rotated 180 deg", 0.0f, 0.0f, 0, 3},
        {"Transposed (Swap XY)", 0.0f, 0.0f, 0, 4},
        {"Transposed + Invert Y", 0.0f, 0.0f, 0, 5},
    };

    const int tol2 = (int)kTolerancePx * kTolerancePx;

    for (int c = 0; c < 6; c++) {
        float sum_err = 0.0f;
        float max_err = 0.0f;
        uint8_t passes = 0;

        for (uint8_t i = 0; i < kTargetCount; i++) {
            const int16_t gx = m_got_x[i];
            const int16_t gy = m_got_y[i];
            int px = gx;
            int py = gy;

            switch (c) {
                case 0: // Normal
                    px = gx;
                    py = gy;
                    break;
                case 1: // Invert Y
                    px = gx;
                    py = (DISPLAY_HEIGHT - 1) - gy;
                    break;
                case 2: // Invert X
                    px = (DISPLAY_WIDTH - 1) - gx;
                    py = gy;
                    break;
                case 3: // Rotated 180
                    px = (DISPLAY_WIDTH - 1) - gx;
                    py = (DISPLAY_HEIGHT - 1) - gy;
                    break;
                case 4: // Transposed
                    px = (int)((float)gy * (float)DISPLAY_WIDTH / (float)DISPLAY_HEIGHT);
                    py = (int)((float)gx * (float)DISPLAY_HEIGHT / (float)DISPLAY_WIDTH);
                    break;
                case 5: // Transposed + Invert Y
                    px = (int)((float)gy * (float)DISPLAY_WIDTH / (float)DISPLAY_HEIGHT);
                    py = (DISPLAY_HEIGHT - 1) - (int)((float)gx * (float)DISPLAY_HEIGHT / (float)DISPLAY_WIDTH);
                    break;
            }

            const int ddx = px - kTargets[i].x;
            const int ddy = py - kTargets[i].y;
            const float dist = sqrtf((float)(ddx * ddx + ddy * ddy));
            sum_err += dist;
            if (dist > max_err) max_err = dist;
            if ((ddx * ddx + ddy * ddy) <= tol2) passes++;
        }

        cands[c].mean_err_px = sum_err / (float)kTargetCount;
        cands[c].max_err_px = max_err;
        cands[c].pass_count = passes;
    }

    // Determine candidate with lowest mean error
    int best_c = 0;
    for (int c = 1; c < 6; c++) {
        if (cands[c].mean_err_px < cands[best_c].mean_err_px) {
            best_c = c;
        }
    }

#ifndef DASHCUE_NATIVE_TEST
    Serial.println("================================================================================");
    Serial.println("TOUCH ERROR MAPPING & ROTATION ANALYSIS (5 Targets):");
    for (uint8_t i = 0; i < kTargetCount; i++) {
        Serial.printf("  Target %u (%-12s): Exp=(%3d, %3d) Got=(%3d, %3d) Raw=(%4d, %4d)\n",
                      i + 1, kTargets[i].name,
                      kTargets[i].x, kTargets[i].y,
                      m_got_x[i], m_got_y[i],
                      m_got_raw_x[i], m_got_raw_y[i]);
    }
    Serial.println("--------------------------------------------------------------------------------");
    for (int c = 0; c < 6; c++) {
        Serial.printf("  [%d] %-24s : Mean = %5.1f px | Max = %5.1f px | %u/5 within tol%s\n",
                      c, cands[c].name,
                      cands[c].mean_err_px, cands[c].max_err_px,
                      cands[c].pass_count,
                      (c == best_c) ? "  <-- BEST" : "");
    }
    Serial.println("================================================================================");
#endif

    // Apply the winning rotation / alignment
    const Candidate &winner = cands[best_c];
    if (winner.action_id == 1) { // Invert Y
        touch_hal_set_invert_y(true);
        touch_hal_set_invert_x(false);
        display_hal_set_rotation(false);
#ifndef DASHCUE_NATIVE_TEST
        Serial.println("TOUCH: Selected and applied INVERT Y mapping -> Saved to NVS!");
#endif
    } else if (winner.action_id == 2) { // Invert X
        touch_hal_set_invert_x(true);
        touch_hal_set_invert_y(false);
        display_hal_set_rotation(false);
#ifndef DASHCUE_NATIVE_TEST
        Serial.println("TOUCH: Selected and applied INVERT X mapping -> Saved to NVS!");
#endif
    } else if (winner.action_id == 3) { // Rotated 180
        touch_hal_set_invert_x(false);
        touch_hal_set_invert_y(false);
        display_hal_set_rotation(true);
#ifndef DASHCUE_NATIVE_TEST
        Serial.println("TOUCH: Selected and applied ROTATED 180 mapping -> Saved to NVS!");
#endif
    } else if (winner.action_id == 0) { // Normal
        touch_hal_set_invert_x(false);
        touch_hal_set_invert_y(false);
        display_hal_set_rotation(false);
#ifndef DASHCUE_NATIVE_TEST
        Serial.println("TOUCH: Confirmed NORMAL mapping optimal -> Preserved!");
#endif
    }

    // Hide crosshairs
    lv_obj_add_flag(m_crosshair_h, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_crosshair_v, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_crosshair_dot, LV_OBJ_FLAG_HIDDEN);

    m_pass = (winner.mean_err_px <= (float)kTolerancePx);

    char title_buf[80];
    snprintf(title_buf, sizeof(title_buf), "TOUCH ALIGNED: %s", winner.name);
    lv_label_set_text(m_instruct_label, title_buf);
    lv_obj_set_style_text_color(m_instruct_label, UI_COLOR_OK, 0);

    char res_buf[192];
    snprintf(res_buf, sizeof(res_buf),
             "Best: %s (%u/5 pass)\nMean Error: %.1f px (Max: %.1f px)\nAlignment saved to NVS! Loading HUD...",
             winner.name, winner.pass_count,
             winner.mean_err_px, winner.max_err_px);
    lv_label_set_text(m_result_label, res_buf);
    lv_obj_set_style_text_color(m_result_label, UI_COLOR_OK, 0);
}
