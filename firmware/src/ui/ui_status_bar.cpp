#include "ui_status_bar.h"
#include "ui_theme.h"
#include <stdio.h>

UiStatusBar::UiStatusBar()
    : m_container(nullptr),
      m_ble_icon(nullptr),
      m_phone_label(nullptr),
      m_speed_label(nullptr),
      m_limit_label(nullptr),
      m_clock_label(nullptr),
      m_battery_label(nullptr),
      m_stale_badge(nullptr),
      m_accent(0xE58B42),
      m_connected(false),
      m_pairing(false),
      m_imperial(false),
      m_last_speed_kmh_x10(0) {}

void UiStatusBar::create(lv_obj_t *parent) {
    m_container = lv_obj_create(parent);
    lv_obj_set_size(m_container, UI_SCREEN_W, UI_STATUS_BAR_H);
    lv_obj_set_pos(m_container, 0, 0);
    lv_obj_set_style_bg_color(m_container, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(m_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(m_container, 0, 0);
    lv_obj_set_style_radius(m_container, 0, 0);
    lv_obj_set_style_pad_all(m_container, 2, 0);
    lv_obj_remove_flag(m_container, LV_OBJ_FLAG_SCROLLABLE);

    // Seamless borderless transition to workspace
    lv_obj_set_style_border_width(m_container, 0, 0);

    // Left: BLE indicator
    m_ble_icon = lv_label_create(m_container);
    lv_label_set_text(m_ble_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(m_ble_icon, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(m_ble_icon, UI_FONT_SM, 0);
    lv_obj_align(m_ble_icon, LV_ALIGN_LEFT_MID, 4, 0);

    // Phone Name
    m_phone_label = lv_label_create(m_container);
    lv_label_set_text(m_phone_label, "DashCue");
    lv_obj_set_style_text_color(m_phone_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_phone_label, UI_FONT_SM, 0);
    lv_obj_align(m_phone_label, LV_ALIGN_LEFT_MID, 22, 0);

    // Speed indicator (only visible while moving; kept out of the centre so
    // the clock lands on the exact display centre line).
    m_speed_label = lv_label_create(m_container);
    lv_label_set_text(m_speed_label, "");
    lv_obj_set_style_text_color(m_speed_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(m_speed_label, UI_FONT_SM, 0);
    lv_obj_align(m_speed_label, LV_ALIGN_CENTER, -78, 0);

    // Speed-limit sign. Hidden when no source publishes a limit; a blank white
    // disc would imply "no limit" and that is a different claim entirely.
    m_limit_label = lv_label_create(m_container);
    lv_label_set_text(m_limit_label, "");
    lv_obj_set_style_bg_color(m_limit_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(m_limit_label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(m_limit_label, 9, 0);
    lv_obj_set_style_pad_hor(m_limit_label, 4, 0);
    lv_obj_set_style_text_color(m_limit_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(m_limit_label, UI_FONT_SM, 0);
    lv_obj_align(m_limit_label, LV_ALIGN_CENTER, -30, 0);
    lv_obj_add_flag(m_limit_label, LV_OBJ_FLAG_HIDDEN);

    // Centre: Clock
    m_clock_label = lv_label_create(m_container);
    lv_label_set_text(m_clock_label, "12:00");
    lv_obj_set_style_text_color(m_clock_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_clock_label, UI_FONT_SM, 0);
    lv_obj_align(m_clock_label, LV_ALIGN_CENTER, 0, 0);

    // Far-right: link-stale badge. A bare yellow "!" is unreadable at a
    // glance, so the badge names the condition instead of decorating it.
    m_stale_badge = lv_label_create(m_container);
    lv_label_set_text(m_stale_badge, "LINK");
    lv_obj_set_style_text_color(m_stale_badge, UI_COLOR_WARN, 0);
    lv_obj_set_style_text_font(m_stale_badge, UI_FONT_SM, 0);
    lv_obj_align(m_stale_badge, LV_ALIGN_RIGHT_MID, -58, 0);
    lv_obj_add_flag(m_stale_badge, LV_OBJ_FLAG_HIDDEN);

    // Far-right: Battery. Unknown until the phone reports it: a hardcoded
    // "100%" is worse than an honest dash on a device powered from a bank.
    m_battery_label = lv_label_create(m_container);
    lv_label_set_text(m_battery_label, "-- " LV_SYMBOL_BATTERY_EMPTY);
    lv_obj_set_style_text_color(m_battery_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_battery_label, UI_FONT_SM, 0);
    lv_obj_align(m_battery_label, LV_ALIGN_RIGHT_MID, -4, 0);
}

void UiStatusBar::setAccent(uint32_t rgb) {
    m_accent = rgb ? rgb : 0xE58B42;
    if (m_speed_label) {
        lv_obj_set_style_text_color(m_speed_label, lv_color_hex(m_accent), 0);
    }
    // Re-derive only the connected colour; pairing and offline are semantic
    // states (warning / inert) and must not become "the album is orange".
    if (m_ble_icon && m_connected && !m_pairing) {
        lv_obj_set_style_text_color(m_ble_icon, lv_color_hex(m_accent), 0);
        if (m_phone_label) {
            lv_obj_set_style_text_color(m_phone_label, lv_color_hex(m_accent), 0);
        }
    }
}

void UiStatusBar::setBackdropColor(uint32_t rgb) {
    if (m_container) {
        lv_obj_set_style_bg_color(m_container, lv_color_hex(rgb), 0);
    }
}

void UiStatusBar::refreshTheme(void) {
    if (!m_container) return;
    lv_obj_set_style_bg_color(m_container, UI_COLOR_BG, 0);
    if (m_phone_label) {
        lv_obj_set_style_text_color(m_phone_label, UI_COLOR_TEXT, 0);
    }
    if (m_clock_label) {
        lv_obj_set_style_text_color(m_clock_label, UI_COLOR_TEXT, 0);
    }
    if (m_battery_label) {
        lv_obj_set_style_text_color(m_battery_label, UI_COLOR_TEXT, 0);
    }
    if (m_ble_icon) {
        setBleStatus(m_connected, m_pairing);
    }
}

void UiStatusBar::setBleStatus(bool connected, bool pairing) {
    if (!m_ble_icon) return;
    m_connected = connected;
    m_pairing = pairing;
    lv_color_t color = UI_COLOR_TEXT_DIM;
    if (pairing) {
        color = UI_COLOR_WARN;
    } else if (connected) {
        color = lv_color_hex(m_accent);
    }
    // The wordmark uses the same state colour as the link glyph, matching the
    // phone's cockpit bar (cyan connected, amber pairing, dim offline).
    lv_obj_set_style_text_color(m_ble_icon, color, 0);
    if (m_phone_label) {
        lv_obj_set_style_text_color(m_phone_label, color, 0);
    }
}

void UiStatusBar::setPhoneName(const char *name) {
    if (!m_phone_label) return;
    lv_label_set_text(m_phone_label, (name && name[0]) ? name : "DashCue HUD");
}

void UiStatusBar::setClock(const char *time_str) {
    if (!m_clock_label) return;
    lv_label_set_text(m_clock_label, (time_str && time_str[0]) ? time_str : "--:--");
}

void UiStatusBar::setClockFromPhone(uint64_t epoch_ms, int32_t tz_offset_min) {
    if (!m_clock_label || epoch_ms < 1000000000000ULL) return;

    // The HUD has no RTC. Derive local wall time from the phone heartbeat
    // (UTC epoch + the zone offset the phone supplied), so the clock is the
    // same minute on both surfaces instead of a frozen boot-time placeholder.
    int64_t local_min = (int64_t)(epoch_ms / 60000ULL) + (int64_t)tz_offset_min;
    int32_t minutes_of_day = (int32_t)(((local_min % 1440) + 1440) % 1440);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes_of_day / 60, minutes_of_day % 60);
    lv_label_set_text(m_clock_label, buf);
}

void UiStatusBar::setBattery(uint32_t percent) {
    if (!m_battery_label) return;
    if (percent == 0 || percent > 100) {
        lv_label_set_text(m_battery_label, "-- " LV_SYMBOL_BATTERY_EMPTY);
        lv_obj_set_style_text_color(m_battery_label, UI_COLOR_TEXT_DIM, 0);
        return;
    }
    char buf[24];
    const char *icon = LV_SYMBOL_BATTERY_FULL;
    if (percent <= 20) icon = LV_SYMBOL_BATTERY_EMPTY;
    else if (percent <= 50) icon = LV_SYMBOL_BATTERY_2;
    else if (percent <= 80) icon = LV_SYMBOL_BATTERY_3;

    snprintf(buf, sizeof(buf), "%u%% %s", (unsigned)percent, icon);
    lv_label_set_text(m_battery_label, buf);

    if (percent <= 20) {
        lv_obj_set_style_text_color(m_battery_label, UI_COLOR_DANGER, 0);
    } else {
        lv_obj_set_style_text_color(m_battery_label, UI_COLOR_TEXT, 0);
    }
}

void UiStatusBar::setImperial(bool imperial) {
    if (m_imperial == imperial) return;
    m_imperial = imperial;
    setSpeed(m_last_speed_kmh_x10);
}

void UiStatusBar::setSpeed(int32_t speed_kmh_x10) {
    m_last_speed_kmh_x10 = speed_kmh_x10;
    if (!m_speed_label) return;
    if (speed_kmh_x10 > 0) {
        char buf[16];
        if (m_imperial) {
            const int mph = (int)((speed_kmh_x10 / 10.0f) * 0.621371f + 0.5f);
            snprintf(buf, sizeof(buf), "%d mph", mph);
        } else {
            snprintf(buf, sizeof(buf), "%d km/h", (int)(speed_kmh_x10 / 10));
        }
        lv_label_set_text(m_speed_label, buf);
        lv_obj_remove_flag(m_speed_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(m_speed_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiStatusBar::setSpeedLimit(uint32_t limit_kmh) {
    if (!m_limit_label) return;
    if (limit_kmh == 0 || limit_kmh > 200) {
        lv_obj_add_flag(m_limit_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", (unsigned)limit_kmh);
    lv_label_set_text(m_limit_label, buf);
    lv_obj_remove_flag(m_limit_label, LV_OBJ_FLAG_HIDDEN);
}

void UiStatusBar::setStale(bool stale) {
    if (!m_stale_badge) return;
    if (stale) {
        lv_obj_remove_flag(m_stale_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(m_stale_badge, LV_OBJ_FLAG_HIDDEN);
    }
}
