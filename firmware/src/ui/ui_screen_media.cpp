#include "ui_screen_media.h"
#include "ui_theme.h"
#include "artwork_store.h"
#include "state/ipc_queues.h"
#include "state/app_state.h"
#include "ui_command_id.h"
#include "dashcue_config.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

enum MediaBtnId {
    BTN_VOL_DOWN = 0,
    BTN_PREV = 1,
    BTN_PLAY_PAUSE = 2,
    BTN_NEXT = 3,
    BTN_VOL_UP = 4
};

// Split Android Auto layout on the 320x168 workspace:
//   Left (x: 10..130):
//     y 8..128: 120x120 hero album art (116 px inner image)
//     y 132..166: Driver-safe Volume controls ([Vol-] and [Vol+], 56x34 px each with ext click)
//   Right (x: 138..312, width 174 px):
//     y 8: App name badge (138, 8, w=96) and Stale/Warning chip (236, 8, w=74)
//     y 30: Large Title (font LG, bold, ellipsized)
//     y 54: Muted Artist (font MD, ellipsized)
//     y 78: Full-width Scrubber bar (touch hitbox 44 px tall)
//     y 104: Prev, Play/Pause, Next controls (48 px tall)


// Blend 35% toward white so a saturated album hue still clears a 3:1 graphic /
// 4.5:1 small-text bar against the near-black band. Pure red tops out around
// L=0.21 no matter how bright its red channel is, so a max-channel floor alone
// is not enough for text. Mirrored by AlbumColorExtractor.liftForDarkSurface.
static inline uint32_t lift_for_dark(uint32_t rgb) {
    const uint32_t r = (((rgb >> 16) & 0xFF) * 65 + 255 * 35) / 100;
    const uint32_t g = (((rgb >> 8) & 0xFF) * 65 + 255 * 35) / 100;
    const uint32_t b = ((rgb & 0xFF) * 65 + 255 * 35) / 100;
    return (r << 16) | (g << 8) | b;
}

// Universal brand color palette fallback for every media application
static uint32_t fallback_app_color(const char *app_name) {
    if (!app_name || !app_name[0]) return 0xE58B42; // Polestar amber brand accent
    if (strstr(app_name, "Spotify")) return 0x1DB954;       // Spotify green
    if (strstr(app_name, "YouTube")) return 0xE62117;       // YouTube red
    if (strstr(app_name, "Apple")) return 0xFA2D48;         // Apple Music rose
    if (strstr(app_name, "SoundCloud")) return 0xFF5500;    // SoundCloud orange
    if (strstr(app_name, "Amazon")) return 0x00A8E1;        // Amazon cyan
    if (strstr(app_name, "Jio") || strstr(app_name, "Saavn")) return 0x2BC5B4; // JioSaavn teal
    if (strstr(app_name, "Audible")) return 0xF8991C;       // Audible gold
    if (strstr(app_name, "Pocket")) return 0xF43E37;        // Pocket Casts crimson
    if (strstr(app_name, "VLC")) return 0xFF8800;           // VLC orange
    if (strstr(app_name, "Gaana")) return 0xE72C30;         // Gaana red
    if (strstr(app_name, "Wynk")) return 0xED1C24;          // Wynk ruby
    if (strstr(app_name, "Poweramp")) return 0x007ACC;      // Poweramp cobalt
    if (strstr(app_name, "Tidal")) return 0x00FFFF;         // Tidal cyan
    if (strstr(app_name, "Deezer")) return 0xA238FF;        // Deezer violet
    if (strstr(app_name, "Bandcamp")) return 0x1DA0C3;      // Bandcamp teal
    if (strstr(app_name, "Audiomack")) return 0xFFA200;     // Audiomack amber
    if (strstr(app_name, "Chrome") || strstr(app_name, "Browser")) return 0x4285F4;

    // Hash-based vibrant hue for any other third-party player
    uint32_t h = 5381;
    for (const char *p = app_name; *p; ++p) h = ((h << 5) + h) + (uint8_t)*p;
    uint8_t r = (uint8_t)(80 + (h & 0x6F));
    uint8_t g = (uint8_t)(80 + ((h >> 8) & 0x6F));
    uint8_t b = (uint8_t)(80 + ((h >> 16) & 0x6F));
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

UiScreenMedia::UiScreenMedia()
    : m_screen(nullptr),
      m_title_label(nullptr),
      m_artist_label(nullptr),
      m_app_badge(nullptr),
      m_app_label(nullptr),
      m_progress_bar(nullptr),
      m_time_label(nullptr),
      m_volume_label(nullptr),
      m_volume_toast_until(0),
      m_stale_label(nullptr),
      m_btn_prev(nullptr),
      m_btn_play_pause(nullptr),
      m_btn_next(nullptr),
      m_play_pause_label(nullptr),
      m_is_playing(false),
      m_duration_ms(0),
      m_snapshot_version(0),
      m_position_ms(0),
      m_position_at_update_ms(0),
      m_playback_speed(0.0f),
      m_scrubbing(false),
      m_scrub_target_ms(0),
      m_vol_btn_held(-1),
      m_vol_press_start_ms(0),
      m_vol_last_step_ms(0),
      m_volume_pct(0),
      m_muted(false),
      m_stale(true),
      m_accent_rgb(0),
      m_accent_ui_rgb(0),
      m_accent_listener(nullptr),
      m_accent_ctx(nullptr) {
    m_last_title[0] = '\0';
    m_last_artist[0] = '\0';
    m_last_app[0] = '\0';
}

void UiScreenMedia::create(lv_obj_t *parent) {
    if (m_screen) return;
    m_screen = lv_obj_create(parent);
    lv_obj_set_size(m_screen, UI_SCREEN_W, UI_WORKSPACE_H);
    lv_obj_set_pos(m_screen, 0, UI_WORKSPACE_Y);
    lv_obj_set_style_bg_color(m_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(m_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(m_screen, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_width(m_screen, 0, 0);
    lv_obj_set_style_radius(m_screen, 0, 0);
    lv_obj_set_style_pad_all(m_screen, 0, 0);
    lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_SCROLLABLE);
    // Solid high-contrast dark cockpit background permanently set; no background gradient or redraw invalidation.

    // Minimal Cockpit Media Layout (zero artwork box, full-width typography & driver-safe controls):
    // Top Row: App Name badge (left) & Link warning (right)
    // Center Hero: Big crisp Song Title, dedicated Artist line
    // Scrubber: Full-width progress bar with elapsed/duration time readout
    // Bottom Controls: Symmetrical 5-button automotive row [Vol-] [Prev] [Play/Pause] [Next] [Vol+]


    // 1. App badge (top-left)
    m_app_label = lv_label_create(m_screen);
    lv_label_set_text(m_app_label, "DashCue");
    lv_obj_set_style_text_color(m_app_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(m_app_label, UI_FONT_SM, 0);
    lv_obj_set_style_bg_color(m_app_label, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(m_app_label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(m_app_label, 6, 0);
    lv_obj_set_style_pad_hor(m_app_label, 10, 0);
    lv_obj_set_style_pad_ver(m_app_label, 4, 0);
    lv_obj_set_pos(m_app_label, 24, 8);
    lv_obj_set_size(m_app_label, 160, 24);
    lv_label_set_long_mode(m_app_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    // 2. Stale warning pill (top-right, hidden by default)
    m_stale_label = lv_label_create(m_screen);
    lv_label_set_text(m_stale_label, LV_SYMBOL_WARNING " STALE");
    lv_obj_set_style_text_color(m_stale_label, UI_COLOR_WARN, 0);
    lv_obj_set_style_text_font(m_stale_label, UI_FONT_SM, 0);
    lv_obj_set_style_bg_color(m_stale_label, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(m_stale_label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(m_stale_label, 6, 0);
    lv_obj_set_style_pad_hor(m_stale_label, 10, 0);
    lv_obj_set_style_pad_ver(m_stale_label, 4, 0);
    lv_obj_set_pos(m_stale_label, UI_SCREEN_W - 114, 8);
    lv_obj_set_size(m_stale_label, 90, 24);
    lv_obj_set_style_text_align(m_stale_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(m_stale_label, LV_OBJ_FLAG_HIDDEN);

    // 3. Hero Track Title label (centered, full width)
    m_title_label = lv_label_create(m_screen);
    lv_label_set_text(m_title_label, "No Media Playing");
    lv_obj_set_style_text_color(m_title_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_title_label, UI_FONT_XL, 0);
    lv_obj_set_pos(m_title_label, 24, 38);
    lv_obj_set_size(m_title_label, UI_SCREEN_W - 48, 38);
    lv_obj_set_style_text_align(m_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(m_title_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    // 4. Artist label (centered, full width)
    m_artist_label = lv_label_create(m_screen);
    lv_label_set_text(m_artist_label, "Connect phone via Bluetooth");
    lv_obj_set_style_text_color(m_artist_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(m_artist_label, UI_FONT_LG, 0);
    lv_obj_set_pos(m_artist_label, 24, 78);
    lv_obj_set_size(m_artist_label, UI_SCREEN_W - 48, 28);
    lv_obj_set_style_text_align(m_artist_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(m_artist_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    // 5. Full-width Scrubber Progress bar
    m_progress_bar = lv_bar_create(m_screen);
    lv_obj_set_size(m_progress_bar, UI_SCREEN_W - 48, 12);
    lv_obj_set_pos(m_progress_bar, 24, 114);
    lv_bar_set_range(m_progress_bar, 0, 100);
    lv_bar_set_value(m_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(m_progress_bar, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_bg_color(m_progress_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(m_progress_bar, 6, 0);
    lv_obj_set_style_radius(m_progress_bar, 6, LV_PART_INDICATOR);
    lv_obj_set_ext_click_area(m_progress_bar, 16);
    lv_obj_add_flag(m_progress_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(m_progress_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(m_progress_bar, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_remove_flag(m_progress_bar, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_add_event_cb(m_progress_bar, onProgressEvent, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(m_progress_bar, onProgressEvent, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(m_progress_bar, onProgressEvent, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(m_progress_bar, onProgressEvent, LV_EVENT_PRESS_LOST, this);
    lv_obj_add_event_cb(m_progress_bar, onProgressEvent, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(m_progress_bar, onProgressEvent, LV_EVENT_SHORT_CLICKED, this);

    // 6. Elapsed / Duration Time Readout
    m_time_label = lv_label_create(m_screen);
    lv_label_set_text(m_time_label, "");
    lv_obj_set_style_text_color(m_time_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(m_time_label, UI_FONT_SM, 0);
    lv_obj_set_pos(m_time_label, 24, 132);
    lv_obj_set_size(m_time_label, UI_SCREEN_W - 48, 20);
    lv_obj_set_style_text_align(m_time_label, LV_TEXT_ALIGN_CENTER, 0);

    // 7. Transient Floating Volume Toast
    m_volume_label = lv_label_create(m_screen);
    lv_label_set_text(m_volume_label, "");
    lv_obj_set_style_bg_color(m_volume_label, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_bg_opa(m_volume_label, LV_OPA_90, 0);
    lv_obj_set_style_radius(m_volume_label, 12, 0);
    lv_obj_set_style_pad_hor(m_volume_label, 14, 0);
    lv_obj_set_style_pad_ver(m_volume_label, 4, 0);
    lv_obj_set_style_text_color(m_volume_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_volume_label, UI_FONT_SM, 0);
    lv_obj_set_pos(m_volume_label, (UI_SCREEN_W - 120) / 2, 130);
    lv_obj_set_size(m_volume_label, 120, 26);
    lv_obj_set_style_text_align(m_volume_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(m_volume_label, LV_OBJ_FLAG_HIDDEN);

    // 8. Symmetrical 5-Button Automotive Row across 480 px:
    // [Vol-] (x=24..92, w=68) | [Prev] (x=108..184, w=76) | [Play/Pause] (x=200..280, w=80) | [Next] (x=296..372, w=76) | [Vol+] (x=388..456, w=68)
    struct BtnConfig { int32_t x, w; const char *sym; int id; bool hero; };
    const BtnConfig btn_configs[5] = {
        {24,  68, LV_SYMBOL_VOLUME_MID, BTN_VOL_DOWN, false},
        {108, 76, LV_SYMBOL_PREV,       BTN_PREV,     false},
        {200, 80, LV_SYMBOL_PLAY,       BTN_PLAY_PAUSE, true},
        {296, 76, LV_SYMBOL_NEXT,       BTN_NEXT,     false},
        {388, 68, LV_SYMBOL_VOLUME_MAX, BTN_VOL_UP,   false}
    };

    for (const BtnConfig &cfg : btn_configs) {
        lv_obj_t *btn = lv_button_create(m_screen);
        lv_obj_set_size(btn, cfg.w, 56);
        lv_obj_set_pos(btn, cfg.x, 166);
        lv_obj_set_style_bg_color(btn, cfg.hero ? UI_COLOR_ACCENT : UI_COLOR_SURFACE, 0);
        lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE2, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, UI_COLOR_SURFACE2, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, cfg.hero ? 28 : 12, 0);
        lv_obj_set_ext_click_area(btn, 10);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, cfg.sym);
        lv_obj_set_style_text_color(lbl, cfg.hero ? UI_COLOR_BG : UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, cfg.hero ? UI_FONT_TITLE : UI_FONT_LG, 0);
        lv_obj_center(lbl);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_set_user_data(btn, (void*)(intptr_t)cfg.id);
        lv_obj_add_event_cb(btn, onTransportClicked, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(btn, onTransportClicked, LV_EVENT_SHORT_CLICKED, this);

        if (cfg.id == BTN_VOL_DOWN || cfg.id == BTN_VOL_UP) {
            lv_obj_add_event_cb(btn, onTransportClicked, LV_EVENT_PRESSED, this);
            lv_obj_add_event_cb(btn, onTransportClicked, LV_EVENT_RELEASED, this);
            lv_obj_add_event_cb(btn, onTransportClicked, LV_EVENT_PRESS_LOST, this);
        }

        if (cfg.id == BTN_PREV) m_btn_prev = btn;
        else if (cfg.id == BTN_PLAY_PAUSE) {
            m_btn_play_pause = btn;
            m_play_pause_label = lbl;
        }
        else if (cfg.id == BTN_NEXT) m_btn_next = btn;
    }

    // Install the album gradient + accent theming (falls back to brand amber
    // when there is no artwork yet), then render current volume.
    applyAccent(artwork_store_dominant_rgb());
    renderVolume();
}

void UiScreenMedia::destroy() {
    if (m_screen) lv_obj_delete(m_screen);
    // All LVGL handles belong to the deleted subtree. Reset the controller so
    // late protocol events cannot touch a released label or button, but keep
    // the chrome sink: the manager owns that wiring, not this screen's life.
    const AccentListener cb = m_accent_listener;
    void *const ctx = m_accent_ctx;
    *this = UiScreenMedia();
    m_accent_listener = cb;
    m_accent_ctx = ctx;
}

void UiScreenMedia::show() {
    if (m_screen) lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenMedia::hide() {
    if (m_screen) lv_obj_add_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenMedia::applyAccent(uint32_t rgb, bool animate) {
    (void)animate;
    if (!m_screen) {
        m_accent_rgb = rgb;
        return;
    }

    // rgb == 0 means no artwork (or a fully grey image): fall back to the
    // universal per-app brand color or Polestar amber accent so the backdrop is always themed.
    const uint32_t base = rgb ? rgb : fallback_app_color(m_last_app);
    const uint32_t ui = lift_for_dark(base);

    // Dedup guard: if colors match current state, avoid redundant style calls
    if (m_accent_rgb == base && m_accent_ui_rgb == ui) {
        return;
    }

    m_accent_rgb = base;
    m_accent_ui_rgb = ui;

    // 1-shot widget-only style assignment (zero background invalidation)
    paintAccent();

    // Notify chrome listener (Status Bar & Tab Bar accents) immediately
    if (m_accent_listener) {
        m_accent_listener(m_accent_ui_rgb, m_accent_ctx);
    }
}

void UiScreenMedia::paintAccent() {
    if (!m_screen) return;
    if (m_progress_bar) lv_obj_set_style_bg_color(m_progress_bar, lv_color_hex(m_accent_ui_rgb),
                              LV_PART_INDICATOR);
    if (m_btn_play_pause) lv_obj_set_style_bg_color(m_btn_play_pause, lv_color_hex(m_accent_ui_rgb), 0);
    if (m_app_label) lv_obj_set_style_text_color(m_app_label, lv_color_hex(m_accent_ui_rgb), 0);
}

void UiScreenMedia::setAccentListener(AccentListener cb, void *ctx) {
    m_accent_listener = cb;
    m_accent_ctx = ctx;
    if (cb) cb(m_accent_ui_rgb, ctx);
}

void UiScreenMedia::update(const dashcue_v1_MediaState &state, bool is_stale) {
    if (!m_screen) return;

    m_stale = is_stale;

    // A closed player is a teardown, not a stale frame: paint the empty state so
    // no title or scrubber fill from the finished session survives.
    if (state.status == dashcue_v1_PlaybackStatus_PB_NO_SESSION) {
        teardownMedia();
        return;
    }

    lv_obj_remove_state(m_btn_play_pause, LV_STATE_DISABLED);

    if (state.app_name[0]) {
        snprintf(m_last_app, sizeof(m_last_app), "%s", state.app_name);
        lv_label_set_text(m_app_label, state.app_name);
    }

    // Track change: Title and artist are the identity here (snapshot_version also bumps on pause/seek).
    if (strcmp(state.title, m_last_title) != 0 || strcmp(state.artist, m_last_artist) != 0) {
        snprintf(m_last_title, sizeof(m_last_title), "%s", state.title);
        snprintf(m_last_artist, sizeof(m_last_artist), "%s", state.artist);
        applyAccent(0, true);
    }

    // Distinct Title and Artist labels on the Split Android Auto layout
    const char *title = state.title[0] ? state.title : "No Media Playing";
    lv_label_set_text(m_title_label, title);

    if (m_artist_label) {
        lv_label_set_text(m_artist_label, state.artist[0] ? state.artist : "");
    }

    m_is_playing = (state.status == dashcue_v1_PlaybackStatus_PB_PLAYING);
    if (m_play_pause_label) {
        lv_label_set_text(m_play_pause_label, m_is_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }

    m_duration_ms = state.duration_ms;
    if (!m_scrubbing) {
        m_position_ms = state.position_ms;
    }
    m_playback_speed = m_is_playing ? (state.playback_speed > 0.0f ? state.playback_speed : 1.0f) : 0.0f;
    m_position_at_update_ms = millis();
    // The phone rejects a command whose snapshot_version is not the one it
    // issued; capture the version carried by this MediaState.
    m_snapshot_version = state.snapshot_version;

    m_volume_pct = state.volume_pct > 100 ? 100 : state.volume_pct;
    m_muted = state.muted;
    if (!m_scrubbing) {
        renderProgress();
    }
    renderVolume();

    // A missing session is not a stale one. Furthermore, if the phone link is healthy,
    // paused playback is normal user behavior and must not flash an amber STALE warning.
    const bool has_session = state.status != dashcue_v1_PlaybackStatus_PB_NO_SESSION;
    m_stale = is_stale && has_session && app_state_is_phone_stale();
    if (m_stale) {
        lv_obj_remove_flag(m_stale_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(m_stale_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiScreenMedia::renderVolume() {
    // The authoritative level arrives here but is only surfaced on demand: a
    // permanent readout does not fit the centered layout, and a always-on pill
    // would compete with the artwork. onTransportClicked() shows it on a ±
    // press; this hook only keeps the visible text in sync if the phone changed
    // the volume independently while the pill is still on screen.
    if (!m_volume_label) return;
    if (lv_obj_has_flag(m_volume_label, LV_OBJ_FLAG_HIDDEN)) return;
    char buf[24];
    if (m_muted) {
        lv_label_set_text(m_volume_label, LV_SYMBOL_MUTE " MUTE");
    } else {
        snprintf(buf, sizeof(buf), LV_SYMBOL_VOLUME_MID " %u%%", (unsigned)m_volume_pct);
        lv_label_set_text(m_volume_label, buf);
    }
}



void UiScreenMedia::tick(uint32_t now_ms) {
    if (!m_screen) return;

    // Long-press volume acceleration proportional to touch hold time
    if (m_vol_btn_held >= 0) {
        uint32_t held_ms = now_ms - m_vol_press_start_ms;
        if (held_ms >= 300) {
            uint32_t interval = 160;
            int step = 5;
            if (held_ms >= 2000) {
                interval = 90;
                step = 10;
            } else if (held_ms >= 1000) {
                interval = 120;
                step = 5;
            }
            if ((uint32_t)(now_ms - m_vol_last_step_ms) >= interval) {
                m_vol_last_step_ms = now_ms;
                sendVolumeStep((m_vol_btn_held == BTN_VOL_DOWN) ? -1 : 1, step);
            }
        }
    }

    // Retire the transient volume pill independently of playback.
    if (m_volume_label && m_volume_toast_until != 0 &&
        (int32_t)(now_ms - m_volume_toast_until) >= 0) {
        lv_obj_add_flag(m_volume_label, LV_OBJ_FLAG_HIDDEN);
        m_volume_toast_until = 0;
    }

    // A stale link must freeze the bar: simulating progress from a phone we
    // have not heard from is exactly the ghost state this layer exists to stop.
    if (m_stale || m_scrubbing || !m_is_playing || m_duration_ms <= 0 || m_position_ms < 0) {
        return;
    }
    const int64_t elapsed = (int64_t)(now_ms - m_position_at_update_ms);
    if (elapsed < 0 || elapsed > 5000) return; // a stale anchor must not jump the bar
    int64_t projected = m_position_ms + (int64_t)(elapsed * m_playback_speed);
    if (projected < 0) projected = 0;
    if (projected > m_duration_ms) projected = m_duration_ms;
    m_position_ms = projected;
    m_position_at_update_ms = now_ms;
    renderProgress();
}

void UiScreenMedia::teardownMedia() {
    if (!m_screen) return;

    m_is_playing = false;
    m_duration_ms = 0;
    m_position_ms = 0;
    m_position_at_update_ms = 0;
    m_playback_speed = 0.0f;
    m_scrubbing = false;
    m_last_title[0] = '\0';
    m_last_artist[0] = '\0';
    m_last_app[0] = '\0';

    lv_label_set_text(m_title_label, "No Media Playing - Connect phone");
    if (m_artist_label) lv_label_set_text(m_artist_label, "");
    if (m_play_pause_label) lv_label_set_text(m_play_pause_label, LV_SYMBOL_PLAY);
    lv_obj_add_state(m_btn_play_pause, LV_STATE_DISABLED);
    lv_bar_set_value(m_progress_bar, 0, LV_ANIM_OFF);
    if (m_time_label) lv_label_set_text(m_time_label, "");
    lv_obj_add_flag(m_stale_label, LV_OBJ_FLAG_HIDDEN);
    if (m_volume_label) lv_obj_add_flag(m_volume_label, LV_OBJ_FLAG_HIDDEN);
    m_volume_toast_until = 0;

    // Invalidate any artwork cache and drop the album tint.
    artwork_store_invalidate();
    applyAccent(0, true);
}

void UiScreenMedia::renderProgress() {
    if (!m_progress_bar) return;
    if (m_duration_ms > 0) {
        int64_t clamped = m_position_ms;
        if (clamped < 0) clamped = 0;
        if (clamped > m_duration_ms) clamped = m_duration_ms;
        int32_t pct = (int32_t)((clamped * 100) / m_duration_ms);
        lv_bar_set_value(m_progress_bar, pct, LV_ANIM_OFF);
        if (m_time_label) {
            char time_buf[32];
            int pos_s = (int)(clamped / 1000);
            int dur_s = (int)(m_duration_ms / 1000);
            snprintf(time_buf, sizeof(time_buf), "%d:%02d / %d:%02d",
                     pos_s / 60, pos_s % 60, dur_s / 60, dur_s % 60);
            lv_label_set_text(m_time_label, time_buf);
        }
    } else {
        lv_bar_set_value(m_progress_bar, 0, LV_ANIM_OFF);
        if (m_time_label) lv_label_set_text(m_time_label, "");
    }
}

void UiScreenMedia::showVolumeToast() {
    if (!m_volume_label) return;
    m_volume_toast_until = millis() + 1200;
    lv_obj_remove_flag(m_volume_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(m_volume_label);
    renderVolume();
}

void UiScreenMedia::sendVolumeStep(int dir, int step_pct) {
    UiToProtoMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_CMD_MEDIA;
    msg.data.media.command_id = ui_next_command_id();
    msg.data.media.snapshot_version = m_snapshot_version;
    msg.data.media.action = dashcue_v1_MediaAction_MA_VOL_STEP;
    msg.data.media.arg = dir;

    if (dir < 0) {
        if (!m_muted && m_volume_pct >= (uint32_t)step_pct) {
            m_volume_pct -= (uint32_t)step_pct;
        } else {
            m_volume_pct = 0;
        }
    } else {
        if (m_volume_pct + (uint32_t)step_pct <= 100) {
            m_volume_pct += (uint32_t)step_pct;
        } else {
            m_volume_pct = 100;
        }
        m_muted = false;
    }
    showVolumeToast();
    ipc_send_ui_to_proto(&msg, 0);
    Serial.printf("UI: vol %s by %d%% -> %u%% cmd=%u\n",
                  (dir > 0 ? "UP" : "DOWN"), step_pct, (unsigned)m_volume_pct,
                  (unsigned)msg.data.media.command_id);
}

void UiScreenMedia::onTransportClicked(lv_event_t *e) {
    UiScreenMedia *self = (UiScreenMedia*)lv_event_get_user_data(e);
    if (!self) return;

    lv_obj_t *target = (lv_obj_t*)lv_event_get_current_target(e);
    int btn_id = (int)(intptr_t)lv_obj_get_user_data(target);
    lv_event_code_t code = lv_event_get_code(e);
    const uint32_t now = millis();

    // Volume buttons support immediate tap + held proportional repeat
    if (btn_id == BTN_VOL_DOWN || btn_id == BTN_VOL_UP) {
        if (code == LV_EVENT_PRESSED) {
            self->m_vol_btn_held = btn_id;
            self->m_vol_press_start_ms = now;
            self->m_vol_last_step_ms = now;
            self->sendVolumeStep((btn_id == BTN_VOL_DOWN) ? -1 : 1, 5);
            return;
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            self->m_vol_btn_held = -1;
            return;
        }
        // Suppress redundant CLICKED/SHORT_CLICKED for volume buttons
        if (code == LV_EVENT_CLICKED || code == LV_EVENT_SHORT_CLICKED) {
            return;
        }
    }

    // Transport buttons: Prev, Play/Pause, Next
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_SHORT_CLICKED) {
        return;
    }

    static uint32_t s_last_touch_ms = 0;
    if (now - s_last_touch_ms < 280) return;
    s_last_touch_ms = now;

    UiToProtoMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_CMD_MEDIA;
    msg.data.media.command_id = ui_next_command_id();
    msg.data.media.snapshot_version = self->m_snapshot_version;

    switch (btn_id) {
        case BTN_PREV:
            msg.data.media.action = dashcue_v1_MediaAction_MA_PREV;
            break;

        case BTN_PLAY_PAUSE:
            if (self->m_is_playing) {
                msg.data.media.action = dashcue_v1_MediaAction_MA_PAUSE;
            } else {
                msg.data.media.action = dashcue_v1_MediaAction_MA_PLAY;
            }
            break;

        case BTN_NEXT:
            msg.data.media.action = dashcue_v1_MediaAction_MA_NEXT;
            break;

        default:
            return;
    }

    ipc_send_ui_to_proto(&msg, 0);
    Serial.printf("UI: media btn %d -> action %d cmd=%u snap=%u\n", btn_id,
                  (int)msg.data.media.action, (unsigned)msg.data.media.command_id,
                  (unsigned)self->m_snapshot_version);
}

void UiScreenMedia::onProgressEvent(lv_event_t *e) {
    UiScreenMedia *self = (UiScreenMedia*)lv_event_get_user_data(e);
    if (!self || self->m_duration_ms <= 0) return;

    const lv_event_code_t code = lv_event_get_code(e);

    lv_area_t bar_coords;
    lv_obj_get_coords(self->m_progress_bar, &bar_coords);
    const int32_t width = bar_coords.x2 - bar_coords.x1;
    if (width <= 0) return;

    lv_point_t point = {0, 0};
    bool has_point = false;
    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        lv_indev_get_point(indev, &point);
        // On RELEASED indev point can be 0 or off-screen; verify it's within plausible horizontal span
        if (point.x >= (bar_coords.x1 - 20) && point.x <= (bar_coords.x2 + 20)) {
            has_point = true;
        }
    }

    if (has_point) {
        int32_t rel_x = point.x - bar_coords.x1;
        if (rel_x < 0) rel_x = 0;
        if (rel_x > width) rel_x = width;
        self->m_scrub_target_ms = (int64_t)((int64_t)rel_x * self->m_duration_ms) / width;
    }

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        self->m_scrubbing = true;
        self->m_position_ms = self->m_scrub_target_ms;
        int32_t pct = (int32_t)((self->m_scrub_target_ms * 100) / self->m_duration_ms);
        lv_bar_set_value(self->m_progress_bar, pct, LV_ANIM_OFF);
        return;
    }

    if (code == LV_EVENT_PRESS_LOST) {
        self->m_scrubbing = false;
        self->renderProgress();
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_CLICKED || code == LV_EVENT_SHORT_CLICKED) {
        if (!self->m_scrubbing) return; // Deduplicate: only dispatch once per user interaction
        self->m_scrubbing = false;
        self->m_position_ms = self->m_scrub_target_ms;
        int32_t pct = (int32_t)((self->m_scrub_target_ms * 100) / self->m_duration_ms);
        lv_bar_set_value(self->m_progress_bar, pct, LV_ANIM_OFF);

        UiToProtoMsg msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = IPC_CMD_MEDIA;
        msg.data.media.command_id = ui_next_command_id();
        msg.data.media.snapshot_version = self->m_snapshot_version;
        msg.data.media.action = dashcue_v1_MediaAction_MA_SEEK_ABS;
        msg.data.media.arg = self->m_scrub_target_ms;
        ipc_send_ui_to_proto(&msg, 0);
        Serial.printf("UI: seek dispatched -> target_ms=%lld (%d%%) cmd=%u\n",
                      (long long)self->m_scrub_target_ms, (int)pct, (unsigned)msg.data.media.command_id);
    }
}
