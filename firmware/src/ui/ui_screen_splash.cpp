#include "ui_screen_splash.h"
#include "ui_theme.h"
#include "dashcue_version.h"
#include "dashcue_config.h"
#include "board/board_hal.h"
#include "touch/touch_hal.h"
#include "ui/ui_manager.h"
#include <stdio.h>

#define SPLASH_TIMEOUT_MS 1200 // <= 1.5s per spec

UiScreenSplash::UiScreenSplash()
    : m_screen(nullptr),
      m_logo_title(nullptr),
      m_logo_subtitle(nullptr),
      m_version_label(nullptr),
      m_start_ms(0),
      m_active(false) {}

void UiScreenSplash::create(lv_obj_t *parent) {
    if (m_screen) return;

    m_screen = lv_obj_create(parent);
    lv_obj_set_size(m_screen, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(m_screen, 0, 0);
    lv_obj_add_style(m_screen, &ui_style_screen, 0);
    lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    m_logo_title = lv_label_create(m_screen);
    lv_label_set_text(m_logo_title, "DashCue");
    lv_obj_set_style_text_color(m_logo_title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_logo_title, UI_FONT_TITLE, 0);
    lv_obj_align(m_logo_title, LV_ALIGN_CENTER, 0, -32);

    // Subtitle
    m_logo_subtitle = lv_label_create(m_screen);
    lv_label_set_text(m_logo_subtitle, "AUTOMOTIVE HEADS-UP DISPLAY");
    lv_obj_set_style_text_color(m_logo_subtitle, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(m_logo_subtitle, UI_FONT_SM, 0);
    lv_obj_align(m_logo_subtitle, LV_ALIGN_CENTER, 0, 0);

    // Version & HW revision footer
    m_version_label = lv_label_create(m_screen);
    char ver_buf[64];
    snprintf(ver_buf, sizeof(ver_buf), "fw %s  hw %s",
             DASHCUE_FW_VERSION_STRING, DASHCUE_HW_REVISION);
    lv_label_set_text(m_version_label, ver_buf);
    lv_obj_add_style(m_version_label, &ui_style_text_dim_sm, 0);
    lv_obj_align(m_version_label, LV_ALIGN_BOTTOM_MID, 0, -12);
}

void UiScreenSplash::destroy() {
    if (m_screen) {
        lv_obj_delete(m_screen);
        m_screen = nullptr;
        m_logo_title = nullptr;
        m_logo_subtitle = nullptr;
        m_version_label = nullptr;
        m_active = false;
        m_start_ms = 0;
    }
}

void UiScreenSplash::show() {
    if (m_screen) lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
    m_active = true;
    m_start_ms = 0; // will be set on first update
}

void UiScreenSplash::hide() {
    if (m_screen) lv_obj_add_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
    m_active = false;
}

void UiScreenSplash::update(uint32_t now_ms) {
    if (!m_screen || !m_active) return;
    if (m_start_ms == 0) {
        m_start_ms = now_ms;
        return;
    }

    if ((now_ms - m_start_ms) >= SPLASH_TIMEOUT_MS) {
        m_active = false;

        // Check if SafeMode triggered
        if (board_is_safemode_triggered()) {
            UiManager::instance().navigateTo(UI_SCREEN_SAFEMODE);
        } else if (!touch_hal_is_calibrated()) {
            // Uncalibrated boot (TC-HW-04): unconditional route to Calibration
            UiManager::instance().navigateTo(UI_SCREEN_CALIBRATION);
        } else {
            UiManager::instance().navigateTo(UI_SCREEN_MEDIA);
        }
    }
}
