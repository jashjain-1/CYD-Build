#include "ui_screen_settings.h"
#include "ui_theme.h"
#include "board/board_hal.h"
#include "display/display_hal.h"
#include "state/ipc_queues.h"
#include "state/app_state.h"
#include "ui/ui_manager.h"
#include "ui_command_id.h"
#include "audio/audio_hal.h"
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
      m_row_units(nullptr),
      m_btn_units(nullptr),
      m_lbl_units(nullptr),
      m_btn_orient(nullptr),
      m_lbl_orient(nullptr),
      m_btn_calib(nullptr),
      m_btn_diag(nullptr),
      m_lbl_about(nullptr),
      m_row_sound(nullptr),
      m_btn_spk_dn(nullptr),
      m_btn_spk_up(nullptr),
      m_lbl_spk_vol(nullptr),
      m_btn_eq(nullptr),
      m_btn_sound_mode(nullptr),
      m_row_pair(nullptr),
      m_lbl_pair_title(nullptr),
      m_btn_pair(nullptr),
      m_btn_forget(nullptr),
      m_driving_locked(false),
      m_bonded(false),
      m_connected(false),
      m_inverted(false),
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
    lv_obj_remove_flag(m_lock_banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(m_lock_banner, LV_OBJ_FLAG_HIDDEN);

    m_lock_label = lv_label_create(m_lock_banner);
    lv_label_set_text(m_lock_label, LV_SYMBOL_WARNING " Settings locked while driving (>= 8 km/h)");
    lv_obj_set_style_text_color(m_lock_label, UI_COLOR_BG, 0);
    lv_obj_set_style_text_font(m_lock_label, UI_FONT_SM, 0);
    lv_obj_center(m_lock_label);

    // List container: plain flex column, NOT a list, NOT scrollable.
    // Scrollable lists on a resistive digitizer intercept direct touches,
    // measure inertia and feed back phantom/greedy gestures, so every tap
    // leaks latency and ghost contacts. Rows are direct children with their
    // own buttons, so no list mapping is needed and no scroll chain exists.
    m_list = lv_obj_create(m_screen);
    lv_obj_set_size(m_list, UI_SCREEN_W, UI_WORKSPACE_H);
    lv_obj_set_pos(m_list, 0, 0);
    lv_obj_set_style_bg_opa(m_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m_list, 0, 0);
    lv_obj_set_style_pad_all(m_list, 4, 0);
    lv_obj_set_style_pad_row(m_list, 4, 0);
    lv_obj_set_flex_flow(m_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(m_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(m_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(m_list, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_remove_flag(m_list, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

    auto make_row = [this](const char *title) -> lv_obj_t* {
        lv_obj_t *row = lv_obj_create(m_list);
        lv_obj_set_size(row, UI_SCREEN_W - 16, 48);
        lv_obj_add_style(row, &ui_style_row, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, title);
        lv_obj_add_style(lbl, &ui_style_text_md, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 6, 0);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
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
        lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE2, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, UI_COLOR_ACCENT, LV_STATE_PRESSED);
        lv_obj_set_ext_click_area(btn, 8);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_SHORT_CLICKED, this);
        return btn;
    };

    // 1. Brightness Row (48px) - primary driving buttons >= 56x48 px
    m_row_brightness = make_row("Brightness");
    m_btn_bri_down = make_btn(m_row_brightness, 56, 48, -116, &ui_style_btn_secondary, onBriDownClicked);
    lv_obj_t *l1 = lv_label_create(m_btn_bri_down);
    lv_label_set_text(l1, LV_SYMBOL_MINUS);
    lv_obj_remove_flag(l1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(l1);

    m_lbl_brightness = lv_label_create(m_row_brightness);
    lv_label_set_text(m_lbl_brightness, "80%");
    lv_obj_add_style(m_lbl_brightness, &ui_style_text_accent_md, 0);
    lv_obj_align(m_lbl_brightness, LV_ALIGN_RIGHT_MID, -66, 0);

    m_btn_bri_up = make_btn(m_row_brightness, 56, 48, -4, &ui_style_btn_secondary, onBriUpClicked);
    lv_obj_t *l2 = lv_label_create(m_btn_bri_up);
    lv_label_set_text(l2, LV_SYMBOL_PLUS);
    lv_obj_remove_flag(l2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(l2);

    // Theme row removed: the cockpit is dark-only (spec §12.10 "Building from
    // Black"), so there is no light mode to select and nothing to mirror.

    // 2. Sound Row: alert output (phone vs CYD), volume clamped by the HAL to
    //    90% and by hardware to 0.5 W into 8 ohm (the DAC swing binds first),
    //    and the tone preset.
    m_row_sound = make_row("Sound");
    m_btn_spk_dn = make_btn(m_row_sound, 48, 48, -318, &ui_style_btn_secondary, onSpkDownClicked);
    lv_obj_t *l_spk_dn = lv_label_create(m_btn_spk_dn);
    lv_label_set_text(l_spk_dn, LV_SYMBOL_MINUS);
    lv_obj_remove_flag(l_spk_dn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(l_spk_dn);

    m_lbl_spk_vol = lv_label_create(m_row_sound);
    lv_label_set_text(m_lbl_spk_vol, "30%");
    lv_obj_add_style(m_lbl_spk_vol, &ui_style_text_accent_md, 0);
    lv_obj_align(m_lbl_spk_vol, LV_ALIGN_RIGHT_MID, -268, 0);

    m_btn_spk_up = make_btn(m_row_sound, 48, 48, -216, &ui_style_btn_secondary, onSpkUpClicked);
    lv_obj_t *l_spk_up = lv_label_create(m_btn_spk_up);
    lv_label_set_text(l_spk_up, LV_SYMBOL_PLUS);
    lv_obj_remove_flag(l_spk_up, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(l_spk_up);

    m_btn_eq = make_btn(m_row_sound, 96, 48, -114, &ui_style_btn_secondary, onEqClicked);
    lv_obj_t *l_eq = lv_label_create(m_btn_eq);
    lv_label_set_text(l_eq, "EQ Flat");
    lv_obj_remove_flag(l_eq, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(l_eq);

    m_btn_sound_mode = make_btn(m_row_sound, 108, 48, -4, &ui_style_btn_accent, onSoundModeClicked);
    lv_obj_t *l_mode = lv_label_create(m_btn_sound_mode);
    lv_label_set_text(l_mode, audio_hal_get_output() == AUDIO_OUT_CYD ? "CYD speaker" : "Phone");
    lv_obj_set_style_text_color(l_mode, UI_COLOR_BG, 0);
    lv_obj_remove_flag(l_mode, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(l_mode);

    // 3. Quick actions row: units, rotation, touch test and stats side by side
    //    so every control stays reachable without a scrolling list.
    m_row_units = make_row("");
    m_btn_units = make_btn(m_row_units, 110, 48, -350, &ui_style_btn_secondary, onUnitsClicked);
    m_lbl_units = lv_label_create(m_btn_units);
    lv_label_set_text(m_lbl_units, m_imperial ? "Imperial" : "Metric");
    lv_obj_remove_flag(m_lbl_units, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(m_lbl_units);

    m_btn_orient = make_btn(m_row_units, 110, 48, -236, &ui_style_btn_secondary, onOrientClicked);
    m_lbl_orient = lv_label_create(m_btn_orient);
    lv_label_set_text(m_lbl_orient, "Rotate");
    lv_obj_remove_flag(m_lbl_orient, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(m_lbl_orient);

    m_btn_calib = make_btn(m_row_units, 110, 48, -122, &ui_style_btn_secondary, onCalibClicked);
    lv_obj_t *l_cal = lv_label_create(m_btn_calib);
    lv_label_set_text(l_cal, "Touch test");
    lv_obj_remove_flag(l_cal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(l_cal);

    m_btn_diag = make_btn(m_row_units, 110, 48, -8, &ui_style_btn_secondary, onDiagClicked);
    lv_obj_t *l_diag = lv_label_create(m_btn_diag);
    lv_label_set_text(l_diag, "Stats");
    lv_obj_remove_flag(l_diag, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(l_diag);

    // 4. Bluetooth Pairing / Forget Row (48px)
    m_row_pair = make_row("Bluetooth");
    m_lbl_pair_title = lv_obj_get_child(m_row_pair, 0);
    m_btn_pair = make_btn(m_row_pair, 70, 48, -76, &ui_style_btn_accent, onPairClicked);
    lv_obj_t *l_pair = lv_label_create(m_btn_pair);
    lv_label_set_text(l_pair, "Pair");
    lv_obj_set_style_text_color(l_pair, UI_COLOR_BG, 0);
    lv_obj_remove_flag(l_pair, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(l_pair);

    m_btn_forget = make_btn(m_row_pair, 70, 48, -2, &ui_style_btn_secondary, onForgetClicked);
    lv_obj_t *l_forget = lv_label_create(m_btn_forget);
    lv_label_set_text(l_forget, "Forget");
    lv_obj_remove_flag(l_forget, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(l_forget);

    // 5. Compact credits line: a 48 px row here would push pairing off-screen.
    m_lbl_about = lv_label_create(m_list);
    lv_label_set_text(m_lbl_about, "DashCue v1.0.0 CYD-R1  -  Jash and Varun");
    lv_obj_add_style(m_lbl_about, &ui_style_text_dim_sm, 0);
    lv_obj_remove_flag(m_lbl_about, LV_OBJ_FLAG_CLICKABLE);

    m_brightness = board_get_backlight_brightness();
    setBrightness(m_brightness);
    setOrientation(m_inverted);
    setPairingState(m_bonded, m_connected);

    // Initial values are applied locally only; user edits from here on are
    // mirrored to the phone so both sides converge on one SettingsData.
    m_sync_enabled = true;
}

void UiScreenSettings::sendSettings() {
    if (!m_sync_enabled) return;

    dashcue_v1_SettingsData data = dashcue_v1_SettingsData_init_zero;
    data.brightness_pct = m_brightness;
    // Dark-only cockpit: the HUD never sends a light theme.
    data.theme = dashcue_v1_ThemeMode_TH_DARK;
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
    const bool imperial = m_imperial;
    const uint8_t brightness = m_brightness;
    *this = UiScreenSettings();
    m_inverted = inverted;
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

    set_btn_state(m_btn_units);
    set_btn_state(m_btn_orient);
    set_btn_state(m_btn_calib);
    set_btn_state(m_btn_pair);
    set_btn_state(m_btn_forget);
    set_btn_state(m_btn_diag);
    // Sound output and EQ can change while parked; volume stays available so
    // a driver can quieten a ringing HUD without pulling over.
    set_btn_state(m_btn_eq);
    set_btn_state(m_btn_sound_mode);
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

void UiScreenSettings::refreshSoundRow() {
    if (!m_screen) return;

    char vol_buf[16];
    snprintf(vol_buf, sizeof(vol_buf), "%u%%", (unsigned)audio_hal_get_volume_pct());
    if (m_lbl_spk_vol) lv_label_set_text(m_lbl_spk_vol, vol_buf);

    if (m_btn_eq) {
        lv_obj_t *lbl = lv_obj_get_child(m_btn_eq, 0);
        if (lbl) {
            int8_t b = 0, m = 0, t = 0;
            audio_hal_get_eq(&b, &m, &t);
            const char *preset = "EQ Flat";
            if (b == 6 && m == 0 && t == -2) preset = "EQ Bass";
            else if (b == -2 && m == 2 && t == 6) preset = "EQ Treble";
            else if (b != 0 || m != 0 || t != 0) preset = "EQ Custom";
            lv_label_set_text(lbl, preset);
        }
    }

    if (m_btn_sound_mode) {
        lv_obj_t *lbl = lv_obj_get_child(m_btn_sound_mode, 0);
        if (lbl) {
            lv_label_set_text(lbl,
                              audio_hal_get_output() == AUDIO_OUT_CYD ? "CYD speaker" : "Phone");
        }
    }
}

void UiScreenSettings::setUnits(bool imperial) {
    m_imperial = imperial;
    if (m_lbl_units) {
        lv_label_set_text(m_lbl_units, imperial ? "Imperial" : "Metric");
    }
}

void UiScreenSettings::setOrientation(bool inverted) {
    m_inverted = inverted;
    display_hal_set_orientation(inverted);
    if (m_lbl_orient) {
        lv_label_set_text(m_lbl_orient, inverted ? "Inverted" : "Normal");
    }
}

void UiScreenSettings::onBriDownClicked(lv_event_t *e) {
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (now - s_last_touch_ms < 250) return;
    s_last_touch_ms = now;

    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self) return;
    Serial.println("UI: settings Brightness- clicked");
    if (self->m_brightness > 10) {
        self->setBrightness(self->m_brightness - 10);
        self->sendSettings();
    } else {
        self->setBrightness(10);
        self->sendSettings();
    }
}

void UiScreenSettings::onBriUpClicked(lv_event_t *e) {
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (now - s_last_touch_ms < 250) return;
    s_last_touch_ms = now;

    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self) return;
    Serial.println("UI: settings Brightness+ clicked");
    if (self->m_brightness < 100) {
        self->setBrightness(self->m_brightness + 10);
        self->sendSettings();
    } else {
        self->setBrightness(100);
        self->sendSettings();
    }
}

void UiScreenSettings::onUnitsClicked(lv_event_t *e) {
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (now - s_last_touch_ms < 250) return;
    s_last_touch_ms = now;

    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self || self->m_driving_locked) return;
    Serial.println("UI: settings Units clicked");
    self->setUnits(!self->m_imperial);
    self->sendSettings();
}

void UiScreenSettings::onSpkDownClicked(lv_event_t *e) {
    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self) return;
    uint32_t vol = audio_hal_get_volume_pct();
    vol = (vol > 5) ? (vol - 5) : 0;
    audio_hal_set_volume_pct(vol);
    audio_hal_play_click(); // the volume change is audible, not just a number
    self->refreshSoundRow();
}

void UiScreenSettings::onSpkUpClicked(lv_event_t *e) {
    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self) return;
    uint32_t vol = audio_hal_get_volume_pct();
    // The HAL clamps to AUDIO_MAX_VOLUME_PCT (90%), and the DAC swing ceiling
    // keeps that inside 0.5 W into 8 ohm, so the button can keep repeating at
    // the top without harm.
    audio_hal_set_volume_pct(vol + 5);
    audio_hal_play_click();
    self->refreshSoundRow();
}

void UiScreenSettings::onEqClicked(lv_event_t *e) {
    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self) return;
    const char *preset = audio_hal_cycle_eq_preset();
    if (self->m_btn_eq) {
        lv_obj_t *lbl = lv_obj_get_child(self->m_btn_eq, 0);
        if (lbl) lv_label_set_text(lbl, preset);
    }
    audio_hal_play_click();
}

void UiScreenSettings::onSoundModeClicked(lv_event_t *e) {
    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self) return;
    Serial.println("UI: settings Sound mode clicked");
    const audio_out_t next =
        (audio_hal_get_output() == AUDIO_OUT_CYD) ? AUDIO_OUT_PHONE : AUDIO_OUT_CYD;
    audio_hal_set_output(next);
    if (next == AUDIO_OUT_CYD) {
        audio_hal_play_beep(); // audible confirmation that the HUD now speaks
    }
    self->refreshSoundRow();
}

void UiScreenSettings::onOrientClicked(lv_event_t *e) {
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (now - s_last_touch_ms < 250) return;
    s_last_touch_ms = now;

    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self || self->m_driving_locked) return;
    Serial.println("UI: settings Orientation clicked");
    self->setOrientation(!self->m_inverted);
    self->sendSettings();
}

void UiScreenSettings::onCalibClicked(lv_event_t *e) {
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (now - s_last_touch_ms < 250) return;
    s_last_touch_ms = now;

    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self || self->m_driving_locked) return;
    Serial.println("UI: settings Touch Verify clicked");
    UiManager::instance().navigateTo(UI_SCREEN_CALIBRATION);
}

void UiScreenSettings::onPairClicked(lv_event_t *e) {
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (now - s_last_touch_ms < 250) return;
    s_last_touch_ms = now;

    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self || self->m_driving_locked) return;
    Serial.println("UI: settings Pair clicked");

    // Send pair request command
    UiToProtoMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_CMD_PAIR_PHONE;
    msg.data.pair_phone.timeout_s = 120;
    ipc_send_ui_to_proto(&msg, 0);

    UiManager::instance().navigateTo(UI_SCREEN_PAIRING);
}

void UiScreenSettings::onForgetClicked(lv_event_t *e) {
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (now - s_last_touch_ms < 250) return;
    s_last_touch_ms = now;

    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self || self->m_driving_locked) return;
    Serial.println("UI: settings Forget clicked");

    UiToProtoMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_CMD_FORGET_PHONE;
    ipc_send_ui_to_proto(&msg, 0);
}

void UiScreenSettings::onDiagClicked(lv_event_t *e) {
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (now - s_last_touch_ms < 250) return;
    s_last_touch_ms = now;

    UiScreenSettings *self = (UiScreenSettings*)lv_event_get_user_data(e);
    if (!self || self->m_driving_locked) return;
    Serial.println("UI: settings Diagnostics clicked");
    UiManager::instance().navigateTo(UI_SCREEN_DIAG);
}
