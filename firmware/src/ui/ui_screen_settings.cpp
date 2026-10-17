#include "ui_screen_settings.h"
#include "ui_theme.h"
#include "board/board_hal.h"
#include "display/display_hal.h"
#include "state/ipc_queues.h"
#include "state/app_state.h"
#include "ui/ui_manager.h"
#include "ui_command_id.h"
#include <stdio.h>

UiScreenSettings::UiScreenSettings()
    : m_screen(nullptr),
      m_list(nullptr),
      m_lock_banner(nullptr),
      m_lock_label(nullptr),
      m_row_brightness(nullptr),
      m_btn_bri_down(nullptr),
      m_btn_bri_up(nullptr),
      m_lbl_brightness(nullptr),
      m_row_theme(nullptr),
      m_btn_theme(nullptr),
      m_lbl_theme(nullptr),
      m_row_units(nullptr),
      m_btn_units(nullptr),
      m_lbl_units(nullptr),
      m_row_orient(nullptr),
      m_btn_orient(nullptr),
      m_lbl_orient(nullptr),
      m_row_calib(nullptr),
      m_btn_calib(nullptr),
      m_row_pair(nullptr),
      m_lbl_pair_title(nullptr),
      m_btn_pair(nullptr),
      m_btn_forget(nullptr),
      m_row_diag(nullptr),
      m_btn_diag(nullptr),
      m_row_about(nullptr),
      m_lbl_about(nullptr),
      m_driving_locked(false),
      m_bonded(false),
      m_connected(false),
      m_inverted(false),
      m_dark_theme(true),
      m_imperial(false),
      m_brightness(80),
      m_sync_enabled(false),
      m_settings_version(1) {}

void UiScreenSettings::create(lv_obj_t *parent) {
    if (m_screen) return;
    m_screen = lv_obj_create(parent);
    lv_obj_set_size(m_screen, UI_SCREEN_W, UI_WORKSPACE_H);
    lv_obj_set_pos(m_screen, 0, UI_WORKSPACE_Y);
    lv_obj_add_style(m_screen, &ui_style_screen, 0);
    lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Driving lock banner at top
    m_lock_banner = lv_obj_create(m_screen);
    lv_obj_set_size(m_lock_banner, UI_SCREEN_W, 24);
    lv_obj_set_pos(m_lock_banner, 0, 0);
    lv_obj_set_style_bg_color(m_lock_banner, UI_COLOR_WARN, 0);
    lv_obj_set_style_bg_opa(m_lock_banner, LV_OPA_90, 0);
    lv_obj_set_style_border_width(m_lock_banner, 0, 0);
    lv_obj_set_style_radius(m_lock_banner, 0, 0);
    lv_obj_add_flag(m_lock_banner, LV_OBJ_FLAG_HIDDEN);

    m_lock_label = lv_label_create(m_lock_banner);
    lv_label_set_text(m_lock_label, LV_SYMBOL_WARNING " Settings locked while driving (>= 8 km/h)");
    lv_obj_set_style_text_color(m_lock_label, UI_COLOR_BG, 0);
    lv_obj_set_style_text_font(m_lock_label, UI_FONT_SM, 0);
    lv_obj_center(m_lock_label);

    // List container: non-scrollable on resistive digitizer to prevent tap swallowing
    m_list = lv_obj_create(m_screen);
    lv_obj_set_size(m_list, UI_SCREEN_W, UI_WORKSPACE_H);
    lv_obj_set_pos(m_list, 0, 0);
    lv_obj_set_style_bg_opa(m_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m_list, 0, 0);
    lv_obj_set_style_pad_all(m_list, 4, 0);
    lv_obj_set_style_pad_row(m_list, 4, 0);
    lv_obj_set_flex_flow(m_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(m_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(m_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(m_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(m_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(m_list, UI_SCREEN_W, 0);

    auto make_row = [this](const char *title) -> lv_obj_t* {
        lv_obj_t *row = lv_obj_create(m_list);
        lv_obj_set_size(row, 310, 48);
        lv_obj_add_style(row, &ui_style_row, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, title);
        lv_obj_add_style(lbl, &ui_style_text_md, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 6, 0);
        return row;
    };

    auto make_btn = [this](lv_obj_t *row, int w, int h, int x_ofs, lv_style_t *style, lv_event_cb_t cb) -> lv_obj_t* {
        lv_obj_t *btn = lv_button_create(row);
        lv_obj_set_size(btn, w, h);
        lv_obj_align(btn, LV_ALIGN_RIGHT_MID, x_ofs, 0);
        if (style) {
            lv_obj_add_style(btn, style, 0);
        }
        lv_obj_add_style(btn, &ui_style_disabled, LV_STATE_DISABLED);
        lv_obj_set_ext_click_area(btn, 4);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
        return btn;
    };

    // 1. Brightness Row (48px) - primary driving buttons >= 56x48 px
    m_row_brightness = make_row("Brightness");
    m_btn_bri_down = make_btn(m_row_brightness, 56, 48, -116, &ui_style_btn_secondary, onBriDownClicked);
    lv_obj_t *l1 = lv_label_create(m_btn_bri_down);
    lv_label_set_text(l1, LV_SYMBOL_MINUS);
    lv_obj_center(l1);

    m_lbl_brightness = lv_label_create(m_row_brightness);
    lv_label_set_text(m_lbl_brightness, "80%");
    lv_obj_add_style(m_lbl_brightness, &ui_style_text_accent_md, 0);
    lv_obj_align(m_lbl_brightness, LV_ALIGN_RIGHT_MID, -66, 0);

    m_btn_bri_up = make_btn(m_row_brightness, 56, 48, -4, &ui_style_btn_secondary, onBriUpClicked);
    lv_obj_t *l2 = lv_label_create(m_btn_bri_up);
    lv_label_set_text(l2, LV_SYMBOL_PLUS);
    lv_obj_center(l2);

    // 2. Theme Row (48px)
    m_row_theme = make_row("Theme");
    m_btn_theme = make_btn(m_row_theme, 110, 44, -4, &ui_style_btn_secondary, onThemeClicked);
    m_lbl_theme = lv_label_create(m_btn_theme);
    lv_label_set_text(m_lbl_theme, "Dark");
    lv_obj_center(m_lbl_theme);

    // 3. Units Row (48px)
    m_row_units = make_row("Distance Units");
    m_btn_units = make_btn(m_row_units, 110, 44, -4, &ui_style_btn_secondary, onUnitsClicked);
    m_lbl_units = lv_label_create(m_btn_units);
    lv_label_set_text(m_lbl_units, m_imperial ? "Imperial (mi)" : "Metric (km)");
    lv_obj_center(m_lbl_units);

    // 4. Orientation Row (48px)
    m_row_orient = make_row("Orientation");
    m_btn_orient = make_btn(m_row_orient, 110, 44, -4, &ui_style_btn_secondary, onOrientClicked);
    m_lbl_orient = lv_label_create(m_btn_orient);
    lv_label_set_text(m_lbl_orient, "Normal 0°");
    lv_obj_center(m_lbl_orient);

    // 4. Touch Calibration Row (48px)
    m_row_calib = make_row("Touch Screen");
    m_btn_calib = make_btn(m_row_calib, 110, 44, -4, &ui_style_btn_secondary, onCalibClicked);
    lv_obj_t *l_cal = lv_label_create(m_btn_calib);
    lv_label_set_text(l_cal, "Calibrate");
    lv_obj_center(l_cal);

    // 5. Bluetooth Pairing / Forget Row (48px)
    m_row_pair = make_row("Bluetooth");
    m_lbl_pair_title = lv_obj_get_child(m_row_pair, 0);
    m_btn_pair = make_btn(m_row_pair, 70, 44, -76, &ui_style_btn_accent, onPairClicked);
    lv_obj_t *l_pair = lv_label_create(m_btn_pair);
    lv_label_set_text(l_pair, "Pair");
    lv_obj_set_style_text_color(l_pair, UI_COLOR_BG, 0);
    lv_obj_center(l_pair);

    m_btn_forget = make_btn(m_row_pair, 70, 44, -2, &ui_style_btn_secondary, onForgetClicked);
    lv_obj_t *l_forget = lv_label_create(m_btn_forget);
    lv_label_set_text(l_forget, "Forget");
    lv_obj_center(l_forget);

    // 6. Diagnostics Row (48px)
    m_row_diag = make_row("Diagnostics");
    m_btn_diag = make_btn(m_row_diag, 110, 44, -4, &ui_style_btn_secondary, onDiagClicked);
    lv_obj_t *l_diag = lv_label_create(m_btn_diag);
    lv_label_set_text(l_diag, "View Stats");
    lv_obj_center(l_diag);

    // 7. About Row (48px)
    m_row_about = make_row("About");
    m_lbl_about = lv_label_create(m_row_about);
    lv_label_set_text(m_lbl_about, "DashCue v1.0.0 CYD-R1");
    lv_obj_add_style(m_lbl_about, &ui_style_text_dim_sm, 0);
    lv_obj_align(m_lbl_about, LV_ALIGN_RIGHT_MID, -6, 0);

    // 8. Authors Row (48px)
    lv_obj_t *row_authors = make_row("Made By");
    lv_obj_t *lbl_authors = lv_label_create(row_authors);
    lv_label_set_text(lbl_authors, "Jash and Varun");
    lv_obj_add_style(lbl_authors, &ui_style_text_accent_sm, 0);
    lv_obj_align(lbl_authors, LV_ALIGN_RIGHT_MID, -6, 0);

    m_brightness = board_get_backlight_brightness();
    setBrightness(m_brightness);
    setOrientation(m_inverted);
    lv_label_set_text(m_lbl_theme, m_dark_theme ? "Dark" : "Light");
    setPairingState(m_bonded, m_connected);

    // Initial values are applied locally only; user edits from here on are
    // mirrored to the phone so both sides converge on one SettingsData.
    m_sync_enabled = true;
}

void UiScreenSettings::sendSettings() {
    if (!m_sync_enabled) return;

    dashcue_v1_SettingsData data = dashcue_v1_SettingsData_init_zero;
    data.brightness_pct = m_brightness;
    data.theme = m_dark_theme ? dashcue_v1_ThemeMode_TH_DARK : dashcue_v1_ThemeMode_TH_LIGHT;
    data.orientation = m_inverted ? dashcue_v1_ScreenOrientation_SO_LANDSCAPE_180
                                  : dashcue_v1_ScreenOrientation_SO_LANDSCAPE;
    data.seek_step_s = 10;
    data.units = m_imperial ? dashcue_v1_UnitSystem_UN_IMPERIAL : dashcue_v1_UnitSystem_UN_METRIC;
    data.version = m_settings_version++;

    // Keep the local snapshot authoritative so UiManager does not re-apply a
    // stale phone value between this edit and the phone's echo.
    app_state_on_settings_set(&data);

    UiToProtoMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_CMD_SETTINGS_SET;
    msg.data.settings_set.command_id = ui_next_command_id();
    msg.data.settings_set.has_settings = true;
    msg.data.settings_set.settings = data;
    ipc_send_ui_to_proto(&msg, 0);
}

void UiScreenSettings::destroy() {
    if (m_screen) lv_obj_delete(m_screen);
    // Keep local user preferences across page switches, but no LVGL pointers.
    const bool inverted = m_inverted;
    const bool dark_theme = m_dark_theme;
    const bool imperial = m_imperial;
    const uint8_t brightness = m_brightness;
    *this = UiScreenSettings();
    m_inverted = inverted;
    m_dark_theme = dark_theme;
    m_imperial = imperial;
    m_brightness = brightness;
}

void UiScreenSettings::show() {
    if (m_screen) lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenSettings::hide() {
    if (m_screen) lv_obj_add_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenSettings::setDrivingLock(bool locked, int32_t speed_kmh_x10) {
    m_driving_locked = locked;
    if (!m_screen) return;
    if (m_driving_locked) {
        char buf[64];
        snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " Settings locked (%d km/h >= 8 km/h)",
                 (int)(speed_kmh_x10 / 10));
        lv_label_set_text(m_lock_label, buf);
        lv_obj_remove_flag(m_lock_banner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(m_list, 0, 24);
        lv_obj_set_size(m_list, UI_SCREEN_W, UI_WORKSPACE_H - 24);
    } else {
        lv_obj_add_flag(m_lock_banner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(m_list, 0, 0);
        lv_obj_set_size(m_list, UI_SCREEN_W, UI_WORKSPACE_H);
    }
    updateLockState();
}

void UiScreenSettings::updateLockState() {
    // Non-brightness buttons are disabled when driving locked via state selector
    auto set_btn_state = [this](lv_obj_t *btn) {
        if (!btn) return;
        if (m_driving_locked) {
            lv_obj_add_state(btn, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(btn, LV_STATE_DISABLED);
        }
    };

    set_btn_state(m_btn_theme);
    set_btn_state(m_btn_units);
    set_btn_state(m_btn_orient);
    set_btn_state(m_btn_calib);
    set_btn_state(m_btn_pair);
    set_btn_state(m_btn_forget);
    set_btn_state(m_btn_diag);
}

void UiScreenSettings::setPairingState(bool bonded, bool connected) {
    m_bonded = bonded;
    m_connected = connected;
    if (!m_screen || !m_lbl_pair_title || !m_btn_pair) return;

    if (connected) {
        lv_label_set_text(m_lbl_pair_title, "Bluetooth \xC2\xB7 Connected");
        lv_obj_set_style_text_color(m_lbl_pair_title, UI_COLOR_OK, 0);
        lv_obj_add_flag(m_btn_pair, LV_OBJ_FLAG_HIDDEN);
    } else if (bonded) {
        lv_label_set_text(m_lbl_pair_title, "Bluetooth \xC2\xB7 Paired");
        lv_obj_set_style_text_color(m_lbl_pair_title, UI_COLOR_TEXT, 0);
        lv_obj_add_flag(m_btn_pair, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(m_lbl_pair_title, "Bluetooth");
        lv_obj_set_style_text_color(m_lbl_pair_title, UI_COLOR_TEXT, 0);
        lv_obj_remove_flag(m_btn_pair, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiScreenSettings::setBrightness(uint8_t percent) {
    m_brightness = percent;
    board_set_backlight_brightness(m_brightness);
    if (m_lbl_brightness) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u%%", m_brightness);
        lv_label_set_text(m_lbl_brightness, buf);
    }
}

void UiScreenSettings::setUnits(bool imperial) {
    m_imperial = imperial;
    if (m_lbl_units) {
        lv_label_set_text(m_lbl_units, imperial ? "Imperial (mi)" : "Metric (km)");
    }
}

void UiScreenSettings::setOrientation(bool inverted) {
    m_inverted = inverted;
    display_hal_set_orientation(inverted);
    if (m_lbl_orient) {
        lv_label_set_text(m_lbl_orient, inverted ? "Inverted 180°" : "Normal 0°");
    }
}

void UiScreenSettings::onBriDownClicked(lv_event_t *e) {
    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self) return;
    if (self->m_brightness > 10) {
        self->setBrightness(self->m_brightness - 10);
        self->sendSettings();
    }
}

void UiScreenSettings::onBriUpClicked(lv_event_t *e) {
    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self) return;
    if (self->m_brightness < 100) {
        self->setBrightness(self->m_brightness + 10);
        self->sendSettings();
    }
}

void UiScreenSettings::setThemeDark(bool dark) {
    m_dark_theme = dark;
    if (m_lbl_theme) {
        lv_label_set_text(m_lbl_theme, dark ? "Dark" : "Light");
    }
}

void UiScreenSettings::onThemeClicked(lv_event_t *e) {
    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self || self->m_driving_locked) return;
    self->m_dark_theme = !self->m_dark_theme;
    lv_label_set_text(self->m_lbl_theme, self->m_dark_theme ? "Dark" : "Light");
    UiManager::instance().setThemeDark(self->m_dark_theme);
    self->sendSettings();
}

void UiScreenSettings::onUnitsClicked(lv_event_t *e) {
    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self || self->m_driving_locked) return;
    self->setUnits(!self->m_imperial);
    self->sendSettings();
}

void UiScreenSettings::onOrientClicked(lv_event_t *e) {
    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self || self->m_driving_locked) return;
    self->setOrientation(!self->m_inverted);
    self->sendSettings();
}

void UiScreenSettings::onCalibClicked(lv_event_t *e) {
    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self || self->m_driving_locked) return;
    UiManager::instance().navigateTo(UI_SCREEN_CALIBRATION);
}

void UiScreenSettings::onPairClicked(lv_event_t *e) {
    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self || self->m_driving_locked) return;

    // Send pair request command
    UiToProtoMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_CMD_PAIR_PHONE;
    msg.data.pair_phone.timeout_s = 120;
    ipc_send_ui_to_proto(&msg, 0);

    UiManager::instance().navigateTo(UI_SCREEN_PAIRING);
}

void UiScreenSettings::onForgetClicked(lv_event_t *e) {
    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self || self->m_driving_locked) return;

    UiToProtoMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_CMD_FORGET_PHONE;
    ipc_send_ui_to_proto(&msg, 0);
}

void UiScreenSettings::onDiagClicked(lv_event_t *e) {
    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self || self->m_driving_locked) return;
    UiManager::instance().navigateTo(UI_SCREEN_DIAG);
}
