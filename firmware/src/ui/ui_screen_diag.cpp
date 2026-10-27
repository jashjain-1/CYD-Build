#include "ui_screen_diag.h"
#include "ui_theme.h"
#include "board/board_hal.h"
#include "touch/touch_hal.h"
#include "ui/ui_manager.h"
#include <stdio.h>

#ifndef DASHCUE_NATIVE_TEST
#include <Arduino.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#endif

UiScreenDiag::UiScreenDiag()
    : m_screen(nullptr),
      m_card(nullptr),
      m_info_label(nullptr),
      m_btn_back(nullptr),
      m_last_update_ms(0) {}

void UiScreenDiag::create(lv_obj_t *parent) {
    if (m_screen) return;

    m_screen = lv_obj_create(parent);
    lv_obj_set_size(m_screen, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(m_screen, 0, 0);
    lv_obj_add_style(m_screen, &ui_style_screen, 0);
    lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_SCROLLABLE);

    m_card = lv_obj_create(m_screen);
    lv_obj_set_size(m_card, UI_SCREEN_W - 24, UI_SCREEN_H - 24);
    lv_obj_center(m_card);
    lv_obj_add_style(m_card, &ui_style_card, 0);
    lv_obj_remove_flag(m_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(m_card);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " System Diagnostics");
    lv_obj_set_style_text_color(title, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(title, UI_FONT_LG, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 2);

    m_info_label = lv_label_create(m_card);
    lv_label_set_text(m_info_label, "Gathering metrics...");
    lv_obj_set_style_text_color(m_info_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_info_label, UI_FONT_SM, 0);
    lv_obj_align(m_info_label, LV_ALIGN_TOP_LEFT, 4, 30);

    // Back button (110x48 px driver touch target)
    m_btn_back = lv_button_create(m_card);
    lv_obj_set_size(m_btn_back, 110, 48);
    lv_obj_align(m_btn_back, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_add_style(m_btn_back, &ui_style_btn_secondary, 0);
    lv_obj_set_ext_click_area(m_btn_back, 8);
    lv_obj_remove_flag(m_btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(m_btn_back, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_remove_flag(m_btn_back, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_add_event_cb(m_btn_back, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(m_btn_back, onBackClicked, LV_EVENT_SHORT_CLICKED, this);

    lv_obj_t *lbl_back = lv_label_create(m_btn_back);
    lv_label_set_text(lbl_back, "Back");
    lv_obj_add_style(lbl_back, &ui_style_text_md, 0);
    lv_obj_center(lbl_back);
    lv_obj_remove_flag(lbl_back, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenDiag::destroy() {
    if (m_screen) {
        lv_obj_delete(m_screen);
        m_screen = nullptr;
        m_card = nullptr;
        m_info_label = nullptr;
        m_btn_back = nullptr;
        m_last_update_ms = 0;
    }
}

void UiScreenDiag::show() {
    if (m_screen) {
        lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
        update(0);
    }
}

void UiScreenDiag::hide() {
    if (m_screen) lv_obj_add_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenDiag::update(uint32_t now_ms) {
    if (!m_screen) return;
    if (m_last_update_ms != 0 && (now_ms - m_last_update_ms < 1000)) return;
    m_last_update_ms = now_ms;

    char buf[256];
#ifndef DASHCUE_NATIVE_TEST
    uint32_t free_heap = ESP.getFreeHeap();
    uint32_t min_heap = ESP.getMinFreeHeap();
    uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    uint32_t uptime_s = millis() / 1000;
    uint32_t reset_count = board_get_reset_count();
    uint32_t reset_reason = board_get_last_reset_reason();
    bool cal_ok = touch_hal_is_calibrated();

    snprintf(buf, sizeof(buf),
             "Uptime: %us\n"
             "Free Heap: %u B\n"
             "Min Heap: %u B\n"
             "Largest Block: %u B\n"
             "Reset Count: %u (Reason: %u)\n"
             "Touch Calibrated: %s\n"
             "Display Driver: TFT_eSPI + LVGL 9\n"
             "Touch Bus: Dedicated HSPI (100 Hz)",
             (unsigned)uptime_s, (unsigned)free_heap, (unsigned)min_heap,
             (unsigned)largest_block, (unsigned)reset_count, (unsigned)reset_reason,
             cal_ok ? "YES (Pass)" : "NO (Uncalibrated)");
#else
    snprintf(buf, sizeof(buf), "Native Test Diagnostics");
#endif

    if (m_info_label) {
        lv_label_set_text(m_info_label, buf);
    }
}

void UiScreenDiag::onBackClicked(lv_event_t *e) {
#ifndef DASHCUE_NATIVE_TEST
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (now - s_last_touch_ms < 300) return;
    s_last_touch_ms = now;
#endif
    Serial.println("UI: diag Back clicked");
    UiManager::instance().navigateTo(UI_SCREEN_SETTINGS);
}
