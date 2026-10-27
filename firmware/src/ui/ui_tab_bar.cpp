#include "ui_tab_bar.h"
#include "ui_theme.h"
#include <Arduino.h>

UiTabBar::UiTabBar()
    : m_container(nullptr),
      m_active_tab(UI_TAB_MEDIA),
      m_callback(nullptr),
      m_accent(0xE58B42) {
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        m_buttons[i] = nullptr;
        m_labels[i] = nullptr;
        m_indicators[i] = nullptr;
    }
}

void UiTabBar::create(lv_obj_t *parent, UiTabSelectCallback cb) {
    m_callback = cb;

    m_container = lv_obj_create(parent);
    lv_obj_set_size(m_container, UI_SCREEN_W, UI_TAB_BAR_H);
    lv_obj_set_pos(m_container, 0, UI_SCREEN_H - UI_TAB_BAR_H);
    lv_obj_set_style_bg_color(m_container, UI_COLOR_BG, 0);
    // Seamless borderless transition to workspace
    lv_obj_set_style_border_width(m_container, 0, 0);
    lv_obj_set_style_radius(m_container, 0, 0);
    lv_obj_set_style_pad_all(m_container, 0, 0);
    lv_obj_remove_flag(m_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(m_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(m_container, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_remove_flag(m_container, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

    const char *names[UI_TAB_COUNT] = {
        LV_SYMBOL_AUDIO "\nMedia",
        LV_SYMBOL_GPS "\nNav",
        LV_SYMBOL_SETTINGS "\nMore"
    };

    // Three driver-safe 160x48 targets spanning the entire 480 px width (0 dead zones)
    const int32_t tab_w = UI_SCREEN_W / UI_TAB_COUNT; // 480 / 3 = 160 px
    const int32_t tab_start_x = 0;

    for (int i = 0; i < UI_TAB_COUNT; i++) {
        m_buttons[i] = lv_button_create(m_container);
        lv_obj_set_size(m_buttons[i], tab_w, UI_TAB_BAR_H);
        lv_obj_set_pos(m_buttons[i], tab_start_x + i * tab_w, 0);
        lv_obj_set_style_radius(m_buttons[i], 0, 0);
        lv_obj_set_style_border_width(m_buttons[i], 0, 0);
        lv_obj_set_style_bg_color(m_buttons[i], UI_COLOR_BG, 0);
        lv_obj_set_style_bg_color(m_buttons[i], UI_COLOR_SURFACE2, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(m_buttons[i], LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(m_buttons[i], 2, 0);
        lv_obj_set_ext_click_area(m_buttons[i], 8);

        // Active indicator line at the bottom edge of the tab button.
        m_indicators[i] = lv_obj_create(m_buttons[i]);
        lv_obj_set_size(m_indicators[i], tab_w - 20, 4);
        lv_obj_align(m_indicators[i], LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(m_indicators[i], lv_color_hex(m_accent), 0);
        lv_obj_set_style_radius(m_indicators[i], 2, 0);
        lv_obj_set_style_border_width(m_indicators[i], 0, 0);
        lv_obj_add_flag(m_indicators[i], LV_OBJ_FLAG_HIDDEN);

        // Label
        m_labels[i] = lv_label_create(m_buttons[i]);
        lv_label_set_text(m_labels[i], names[i]);
        lv_obj_set_style_text_align(m_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(m_labels[i], UI_FONT_SM, 0);
        lv_obj_set_style_text_color(m_labels[i], UI_COLOR_TEXT_DIM, 0);
        lv_obj_center(m_labels[i]);

        // Ensure child elements pass clicks directly to the button
        lv_obj_remove_flag(m_labels[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(m_indicators[i], LV_OBJ_FLAG_CLICKABLE);

        // Prevent button drag gestures from swallowing clicks
        lv_obj_remove_flag(m_buttons[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(m_buttons[i], LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_remove_flag(m_buttons[i], LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

        // Store tab index as user data
        lv_obj_set_user_data(m_buttons[i], (void*)(intptr_t)i);
        lv_obj_add_event_cb(m_buttons[i], onTabClicked, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(m_buttons[i], onTabClicked, LV_EVENT_SHORT_CLICKED, this);
    }

    setActiveTab(m_active_tab);
}

void UiTabBar::setAccent(uint32_t rgb) {
    m_accent = rgb ? rgb : 0xE58B42;
    // Re-apply the active state so the label and underline pick it up.
    if (m_container) setActiveTab(m_active_tab);
}

void UiTabBar::setActiveTab(UiTabId tab) {
    if (tab >= UI_TAB_COUNT) return;
    m_active_tab = tab;

    for (int i = 0; i < UI_TAB_COUNT; i++) {
        if (i == (int)tab) {
            lv_obj_set_style_text_color(m_labels[i], lv_color_hex(m_accent), 0);
            lv_obj_set_style_bg_color(m_indicators[i], lv_color_hex(m_accent), 0);
            lv_obj_set_style_bg_color(m_buttons[i], UI_COLOR_SURFACE, 0);
            lv_obj_remove_flag(m_indicators[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_set_style_text_color(m_labels[i], UI_COLOR_TEXT_DIM, 0);
            lv_obj_set_style_bg_color(m_buttons[i], UI_COLOR_BG, 0);
            lv_obj_add_flag(m_indicators[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void UiTabBar::refreshTheme(void) {
    if (!m_container) return;
    lv_obj_set_style_bg_color(m_container, UI_COLOR_BG, 0);
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        if (!m_buttons[i]) continue;
        if (i == (int)m_active_tab) {
            lv_obj_set_style_bg_color(m_buttons[i], UI_COLOR_SURFACE, 0);
            if (m_labels[i]) lv_obj_set_style_text_color(m_labels[i], lv_color_hex(m_accent), 0);
        } else {
            lv_obj_set_style_bg_color(m_buttons[i], UI_COLOR_BG, 0);
            if (m_labels[i]) lv_obj_set_style_text_color(m_labels[i], UI_COLOR_TEXT_DIM, 0);
        }
    }
}

void UiTabBar::setVisible(bool visible) {
    if (!m_container) return;
    if (visible) {
        lv_obj_remove_flag(m_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(m_container);
    } else {
        lv_obj_add_flag(m_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiTabBar::onTabClicked(lv_event_t *e) {
    static uint32_t s_last_tab_ms = 0;
    const uint32_t now = millis();
    if (now - s_last_tab_ms < 200) return;
    s_last_tab_ms = now;

    UiTabBar *self = (UiTabBar*)lv_event_get_user_data(e);
    lv_obj_t *target = (lv_obj_t*)lv_event_get_current_target(e);
    int tab_idx = (int)(intptr_t)lv_obj_get_user_data(target);

    Serial.printf("UI: onTabClicked -> tab_idx %d\n", tab_idx);

    if (self && tab_idx >= 0 && tab_idx < UI_TAB_COUNT) {
        self->setActiveTab((UiTabId)tab_idx);
        if (self->m_callback) {
            self->m_callback((UiTabId)tab_idx);
        }
    }
}
