#include "ui_screen_safemode.h"
#include "ui_theme.h"
#include "board/board_hal.h"
#include "touch/touch_hal.h"
#include "ui/ui_manager.h"
#include <stdio.h>

#ifndef DASHCUE_NATIVE_TEST
#include <Arduino.h>
#include <esp_system.h>
#endif

UiScreenSafeMode::UiScreenSafeMode()
    : m_screen(nullptr),
      m_card(nullptr),
      m_details_label(nullptr),
      m_btn_calib(nullptr),
      m_btn_factory_reset(nullptr),
      m_btn_reboot(nullptr),
      m_crash_count(0),
      m_reset_reason(0) {}

void UiScreenSafeMode::create(lv_obj_t *parent) {
    if (m_screen) return;

    m_screen = lv_obj_create(parent);
    lv_obj_set_size(m_screen, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(m_screen, 0, 0);
    lv_obj_add_style(m_screen, &ui_style_screen, 0);
    lv_obj_set_style_bg_color(m_screen, lv_color_hex(0x1F0A0A), 0); // Emergency dark red
    lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_SCROLLABLE);

    m_card = lv_obj_create(m_screen);
    lv_obj_set_size(m_card, 300, 220);
    lv_obj_center(m_card);
    lv_obj_add_style(m_card, &ui_style_card, 0);
    lv_obj_set_style_border_color(m_card, UI_COLOR_DANGER, 0);
    lv_obj_set_style_border_width(m_card, 2, 0);
    lv_obj_set_style_radius(m_card, 10, 0);
    lv_obj_remove_flag(m_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(m_card);
    lv_label_set_text(title, LV_SYMBOL_WARNING " SAFE MODE ACTIVATED");
    lv_obj_set_style_text_color(title, UI_COLOR_DANGER, 0);
    lv_obj_set_style_text_font(title, UI_FONT_LG, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    m_details_label = lv_label_create(m_card);
    if (m_crash_count > 0) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "Detected %u resets in 60s (Reason: %u).\nSafeMode active to prevent crash loop.",
                 (unsigned)m_crash_count, (unsigned)m_reset_reason);
        lv_label_set_text(m_details_label, buf);
    } else {
        lv_label_set_text(m_details_label, "Triggered by >= 3 resets within 60s.\nBackground sync is suspended.");
    }
    lv_obj_add_style(m_details_label, &ui_style_text_dim_sm, 0);
    lv_obj_set_style_text_align(m_details_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(m_details_label, LV_ALIGN_TOP_MID, 0, 30);

    // 3 Action Buttons (each height 40 px)
    auto make_btn = [this](int y, const char *label, lv_style_t *btn_style, lv_event_cb_t cb) -> lv_obj_t* {
        lv_obj_t *btn = lv_button_create(m_card);
        lv_obj_set_size(btn, 260, 40);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
        if (btn_style) {
            lv_obj_add_style(btn, btn_style, 0);
        }
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label);
        lv_obj_add_style(lbl, &ui_style_text_md, 0);
        lv_obj_center(lbl);
        return btn;
    };

    m_btn_calib = make_btn(72, "Recalibrate Touch", &ui_style_btn_secondary, onCalibClicked);
    m_btn_factory_reset = make_btn(118, "Reset Settings / Bonds", &ui_style_btn_warn, onResetClicked);
    m_btn_reboot = make_btn(164, "Reboot System", &ui_style_btn_danger, onRebootClicked);

    lv_obj_add_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenSafeMode::destroy() {
    if (m_screen) {
        lv_obj_delete(m_screen);
        m_screen = nullptr;
        m_card = nullptr;
        m_details_label = nullptr;
        m_btn_calib = nullptr;
        m_btn_factory_reset = nullptr;
        m_btn_reboot = nullptr;
    }
}

void UiScreenSafeMode::show() {
    if (m_screen) lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenSafeMode::hide() {
    if (m_screen) lv_obj_add_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenSafeMode::setCrashDetails(uint32_t crash_count, uint32_t reset_reason) {
    m_crash_count = crash_count;
    m_reset_reason = reset_reason;
    if (!m_details_label) return;
    char buf[128];
    snprintf(buf, sizeof(buf),
             "Detected %u resets in 60s (Reason: %u).\nSafeMode active to prevent crash loop.",
             (unsigned)crash_count, (unsigned)reset_reason);
    lv_label_set_text(m_details_label, buf);
}

void UiScreenSafeMode::onCalibClicked(lv_event_t *e) {
    (void)e;
    UiManager::instance().navigateTo(UI_SCREEN_CALIBRATION);
}

void UiScreenSafeMode::onResetClicked(lv_event_t *e) {
    (void)e;
    board_reset_crash_tracker();
    touch_hal_clear_calibration();
#ifndef DASHCUE_NATIVE_TEST
    esp_restart();
#endif
}

void UiScreenSafeMode::onRebootClicked(lv_event_t *e) {
    (void)e;
    board_reset_crash_tracker();
#ifndef DASHCUE_NATIVE_TEST
    esp_restart();
#endif
}
