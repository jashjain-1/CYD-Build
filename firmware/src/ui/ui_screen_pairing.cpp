#include "ui_screen_pairing.h"
#include "ui_theme.h"
#include "state/ipc_queues.h"
#include "ui/ui_manager.h"
#include <stdio.h>

UiScreenPairing::UiScreenPairing()
    : m_screen(nullptr),
      m_card(nullptr),
      m_passkey_label(nullptr),
      m_countdown_label(nullptr),
      m_cancel_btn(nullptr),
      m_passkey(0),
      m_seconds_remaining(120),
      m_has_passkey(false) {}

void UiScreenPairing::create(lv_obj_t *parent) {
    if (m_screen) return;

    m_screen = lv_obj_create(parent);
    lv_obj_set_size(m_screen, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(m_screen, 0, 0);
    lv_obj_add_style(m_screen, &ui_style_screen, 0);
    lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Center Card
    m_card = lv_obj_create(m_screen);
    lv_obj_set_size(m_card, 280, 200);
    lv_obj_center(m_card);
    lv_obj_add_style(m_card, &ui_style_card, 0);
    lv_obj_set_style_border_color(m_card, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(m_card, 2, 0);
    lv_obj_set_style_radius(m_card, 12, 0);
    lv_obj_remove_flag(m_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(m_card);
    lv_label_set_text(title, LV_SYMBOL_BLUETOOTH " Bluetooth Pairing");
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, UI_FONT_LG, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *instruct = lv_label_create(m_card);
    lv_label_set_text(instruct, "Confirm 6-digit passkey on your phone:");
    lv_obj_add_style(instruct, &ui_style_text_dim_sm, 0);
    lv_obj_align(instruct, LV_ALIGN_TOP_MID, 0, 28);

    // Passkey box
    lv_obj_t *p_box = lv_obj_create(m_card);
    lv_obj_set_size(p_box, 240, 52);
    lv_obj_align(p_box, LV_ALIGN_CENTER, 0, -4);
    lv_obj_set_style_bg_color(p_box, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_border_width(p_box, 1, 0);
    lv_obj_set_style_border_color(p_box, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_radius(p_box, 8, 0);

    m_passkey_label = lv_label_create(p_box);
    lv_label_set_text(m_passkey_label, "- - - - - -");
    lv_obj_set_style_text_color(m_passkey_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(m_passkey_label, UI_FONT_TITLE, 0);
    lv_obj_center(m_passkey_label);

    // Countdown timer label
    m_countdown_label = lv_label_create(m_card);
    lv_label_set_text(m_countdown_label, "Pairing window: 120s remaining");
    lv_obj_set_style_text_color(m_countdown_label, UI_COLOR_WARN, 0);
    lv_obj_set_style_text_font(m_countdown_label, UI_FONT_SM, 0);
    lv_obj_align(m_countdown_label, LV_ALIGN_BOTTOM_MID, 0, -56);

    // Cancel Button (>= 80x48 px hit target)
    m_cancel_btn = lv_button_create(m_card);
    lv_obj_set_size(m_cancel_btn, 120, 48);
    lv_obj_align(m_cancel_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_add_style(m_cancel_btn, &ui_style_btn_secondary, 0);
    lv_obj_set_style_radius(m_cancel_btn, 8, 0);
    lv_obj_add_event_cb(m_cancel_btn, onCancelClicked, LV_EVENT_CLICKED, this);

    lv_obj_t *cancel_text = lv_label_create(m_cancel_btn);
    lv_label_set_text(cancel_text, "Cancel");
    lv_obj_add_style(cancel_text, &ui_style_text_md, 0);
    lv_obj_center(cancel_text);

    // If passkey was already set, populate it
    if (m_has_passkey) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%u %u %u   %u %u %u",
                 (unsigned)((m_passkey / 100000) % 10),
                 (unsigned)((m_passkey / 10000) % 10),
                 (unsigned)((m_passkey / 1000) % 10),
                 (unsigned)((m_passkey / 100) % 10),
                 (unsigned)((m_passkey / 10) % 10),
                 (unsigned)(m_passkey % 10));
        lv_label_set_text(m_passkey_label, buf);
    }
    char cbuf[48];
    snprintf(cbuf, sizeof(cbuf), "Pairing window: %us remaining", (unsigned)m_seconds_remaining);
    lv_label_set_text(m_countdown_label, cbuf);

    lv_obj_add_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenPairing::destroy() {
    if (m_screen) {
        lv_obj_delete(m_screen);
        m_screen = nullptr;
        m_card = nullptr;
        m_passkey_label = nullptr;
        m_countdown_label = nullptr;
        m_cancel_btn = nullptr;
        m_has_passkey = false;
    }
}

void UiScreenPairing::show() {
    if (m_screen) lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenPairing::hide() {
    if (m_screen) lv_obj_add_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenPairing::setPasskey(uint32_t passkey) {
    m_passkey = passkey;
    m_has_passkey = true; // 000000 is a valid six-digit Bluetooth passkey.
    if (m_passkey_label) {
        char buf[32];
        // Format with spacing: e.g. "8 4 2   9 1 0"
        snprintf(buf, sizeof(buf), "%u %u %u   %u %u %u",
                 (unsigned)((passkey / 100000) % 10),
                 (unsigned)((passkey / 10000) % 10),
                 (unsigned)((passkey / 1000) % 10),
                 (unsigned)((passkey / 100) % 10),
                 (unsigned)((passkey / 10) % 10),
                 (unsigned)(passkey % 10));
        lv_label_set_text(m_passkey_label, buf);
    }
}

void UiScreenPairing::setRemainingSeconds(uint32_t seconds) {
    m_seconds_remaining = seconds;
    if (m_countdown_label) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Pairing window: %us remaining", (unsigned)seconds);
        lv_label_set_text(m_countdown_label, buf);
    }
}

void UiScreenPairing::onCancelClicked(lv_event_t *e) {
    (void)e;
    UiToProtoMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_CMD_CANCEL_PAIRING;
    ipc_send_ui_to_proto(&msg, 0);

    UiManager::instance().navigateTo(UI_SCREEN_SETTINGS);
}
