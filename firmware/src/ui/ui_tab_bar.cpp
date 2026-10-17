#include "ui_tab_bar.h"
#include "ui_theme.h"

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
    lv_obj_remove_flag(m_container, LV_OBJ_FLAG_SCROLLABLE);

    const char *names[UI_TAB_COUNT] = {
        LV_SYMBOL_AUDIO "\nMedia",
        LV_SYMBOL_GPS "\nNav",
        LV_SYMBOL_SETTINGS "\nMore"
    };

    // Three driver-safe 80x48 targets, centred as a group per the HUD spec.
    const int32_t tab_w = 80;
    const int32_t tab_start_x = (UI_SCREEN_W - (tab_w * UI_TAB_COUNT)) / 2;

    for (int i = 0; i < UI_TAB_COUNT; i++) {
        m_buttons[i] = lv_button_create(m_container);
        lv_obj_set_size(m_buttons[i], tab_w, UI_TAB_BAR_H);
        lv_obj_set_pos(m_buttons[i], tab_start_x + i * tab_w, 0);
        lv_obj_set_style_radius(m_buttons[i], 0, 0);
        lv_obj_set_style_border_width(m_buttons[i], 0, 0);
        lv_obj_set_style_bg_color(m_buttons[i], UI_COLOR_BG, 0);
        lv_obj_set_style_bg_opa(m_buttons[i], LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(m_buttons[i], 2, 0);

        // Active indicator line at the bottom edge of the tab button.
        m_indicators[i] = lv_obj_create(m_buttons[i]);
        lv_obj_set_size(m_indicators[i], tab_w - 16, 3);
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

        // Store tab index as user data
        lv_obj_set_user_data(m_buttons[i], (void*)(intptr_t)i);
        lv_obj_add_event_cb(m_buttons[i], onTabClicked, LV_EVENT_CLICKED, this);
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
    } else {
        lv_obj_add_flag(m_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiTabBar::onTabClicked(lv_event_t *e) {
    UiTabBar *self = (UiTabBar*)lv_event_get_user_data(e);
    lv_obj_t *target = (lv_obj_t*)lv_event_get_target(e);
    int tab_idx = (int)(intptr_t)lv_obj_get_user_data(target);

    if (self && tab_idx >= 0 && tab_idx < UI_TAB_COUNT) {
        self->setActiveTab((UiTabId)tab_idx);
        if (self->m_callback) {
            self->m_callback((UiTabId)tab_idx);
        }
    }
}
