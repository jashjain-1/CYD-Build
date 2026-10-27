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
static const int32_t ART_BORDER = 2;
static const int32_t ART_TILE = 160;
// The image sits inside the 2 px border, so it is 156 px on a 160 px tile.
static const int32_t ART_INNER = ART_TILE - ART_BORDER * 2; // 156
static const int32_t ART_X = 14;
static const int32_t ART_Y = 14;

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
      m_artwork_box(nullptr),
      m_artwork_icon(nullptr),
      m_artwork_img(nullptr),
      m_artwork_dsc(),   // value-init: flags/reserved MUST be 0 — see refreshArtwork
      m_artwork_generation(0),
      m_title_label(nullptr),
      m_artist_label(nullptr),
      m_app_badge(nullptr),
      m_app_label(nullptr),
      m_progress_bar(nullptr),
      m_time_label(nullptr),
      m_volume_bar(nullptr),
      m_volume_icon(nullptr),
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

    // 1. Hero album art, left-aligned (120x120 tile).
    m_artwork_box = lv_obj_create(m_screen);
    lv_obj_set_size(m_artwork_box, ART_TILE, ART_TILE);
    lv_obj_set_pos(m_artwork_box, ART_X, ART_Y);
    lv_obj_set_style_bg_color(m_artwork_box, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(m_artwork_box, LV_OPA_COVER, 0); // Isolate dirty-rect redraws
    lv_obj_set_style_border_color(m_artwork_box, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(m_artwork_box, ART_BORDER, 0);
    lv_obj_set_style_radius(m_artwork_box, 12, 0);
    lv_obj_set_style_pad_all(m_artwork_box, 0, 0);
    lv_obj_set_style_clip_corner(m_artwork_box, true, 0);
    lv_obj_remove_flag(m_artwork_box, LV_OBJ_FLAG_SCROLLABLE);

    m_artwork_icon = lv_label_create(m_artwork_box);
    lv_label_set_text(m_artwork_icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(m_artwork_icon, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(m_artwork_icon, UI_FONT_TITLE, 0);
    lv_obj_center(m_artwork_icon);

    // Transient volume pill, centered on the artwork
    m_volume_label = lv_label_create(m_artwork_box);
    lv_label_set_text(m_volume_label, "");
    lv_obj_set_style_bg_color(m_volume_label, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(m_volume_label, LV_OPA_80, 0);
    lv_obj_set_style_radius(m_volume_label, 6, 0);
    lv_obj_set_style_pad_hor(m_volume_label, 8, 0);
    lv_obj_set_style_pad_ver(m_volume_label, 3, 0);
    lv_obj_set_style_text_color(m_volume_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_volume_label, UI_FONT_SM, 0);
    lv_obj_center(m_volume_label);
    lv_obj_add_flag(m_volume_label, LV_OBJ_FLAG_HIDDEN);

    m_artwork_img = lv_image_create(m_artwork_box);
    lv_obj_set_size(m_artwork_img, ART_INNER, ART_INNER);
    lv_obj_center(m_artwork_img);
    // 256 = 1x. 156/64 * 256 = 624. Nearest-neighbour upscale (antialias off)
    // to keep the per-frame transform buffer-free; the render path re-enforces
    // this after every artwork generation change.
    lv_image_set_scale(m_artwork_img, (ART_INNER * 256) / ARTWORK_W);
    lv_image_set_antialias(m_artwork_img, false);
    lv_obj_add_flag(m_artwork_img, LV_OBJ_FLAG_HIDDEN);

    // 2. Volume controls under artwork ([Vol-] and [Vol+], 72x48 px, driver-safe)
    lv_obj_t *btn_vd = lv_button_create(m_screen);
    lv_obj_set_size(btn_vd, 72, 48);
    lv_obj_set_pos(btn_vd, 14, 192);
    lv_obj_set_style_bg_color(btn_vd, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_color(btn_vd, UI_COLOR_SURFACE2, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_vd, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_border_width(btn_vd, 1, 0);
    lv_obj_set_style_radius(btn_vd, 8, 0);
    lv_obj_set_ext_click_area(btn_vd, 10);
    lv_obj_set_user_data(btn_vd, (void*)(intptr_t)BTN_VOL_DOWN);
    lv_obj_remove_flag(btn_vd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(btn_vd, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_remove_flag(btn_vd, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_add_event_cb(btn_vd, onTransportClicked, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(btn_vd, onTransportClicked, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(btn_vd, onTransportClicked, LV_EVENT_PRESS_LOST, this);
    lv_obj_add_event_cb(btn_vd, onTransportClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(btn_vd, onTransportClicked, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t *lbl_vd = lv_label_create(btn_vd);
    lv_label_set_text(lbl_vd, LV_SYMBOL_VOLUME_MID);
    lv_obj_remove_flag(lbl_vd, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_color(lbl_vd, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_vd, UI_FONT_MD, 0);
    lv_obj_center(lbl_vd);

    lv_obj_t *btn_vu = lv_button_create(m_screen);
    lv_obj_set_size(btn_vu, 72, 48);
    lv_obj_set_pos(btn_vu, 94, 192);
    lv_obj_set_style_bg_color(btn_vu, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_color(btn_vu, UI_COLOR_SURFACE2, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_vu, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_border_width(btn_vu, 1, 0);
    lv_obj_set_style_radius(btn_vu, 8, 0);
    lv_obj_set_ext_click_area(btn_vu, 10);
    lv_obj_set_user_data(btn_vu, (void*)(intptr_t)BTN_VOL_UP);
    lv_obj_remove_flag(btn_vu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(btn_vu, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_remove_flag(btn_vu, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_add_event_cb(btn_vu, onTransportClicked, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(btn_vu, onTransportClicked, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(btn_vu, onTransportClicked, LV_EVENT_PRESS_LOST, this);
    lv_obj_add_event_cb(btn_vu, onTransportClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(btn_vu, onTransportClicked, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t *lbl_vu = lv_label_create(btn_vu);
    lv_label_set_text(lbl_vu, LV_SYMBOL_VOLUME_MAX);
    lv_obj_remove_flag(lbl_vu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_color(lbl_vu, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_vu, UI_FONT_MD, 0);
    lv_obj_center(lbl_vu);

    // 3. Right column: App badge + Stale pill (x = 185 .. 475, w = 290 px)
    m_app_label = lv_label_create(m_screen);
    lv_label_set_text(m_app_label, "DashCue");
    lv_obj_set_style_text_color(m_app_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(m_app_label, UI_FONT_SM, 0);
    lv_obj_set_style_bg_color(m_app_label, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(m_app_label, LV_OPA_70, 0);
    lv_obj_set_style_radius(m_app_label, 4, 0);
    lv_obj_set_style_pad_hor(m_app_label, 8, 0);
    lv_obj_set_style_pad_ver(m_app_label, 3, 0);
    lv_obj_set_pos(m_app_label, 185, 12);
    lv_obj_set_size(m_app_label, 130, 22);
    lv_label_set_long_mode(m_app_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR); // Marquee when the app name overflows the badge

    m_stale_label = lv_label_create(m_screen);
    lv_label_set_text(m_stale_label, LV_SYMBOL_WARNING " STALE");
    lv_obj_set_style_text_color(m_stale_label, UI_COLOR_WARN, 0);
    lv_obj_set_style_text_font(m_stale_label, UI_FONT_SM, 0);
    lv_obj_set_style_bg_color(m_stale_label, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(m_stale_label, LV_OPA_70, 0);
    lv_obj_set_style_radius(m_stale_label, 4, 0);
    lv_obj_set_style_pad_hor(m_stale_label, 8, 0);
    lv_obj_set_style_pad_ver(m_stale_label, 3, 0);
    lv_obj_set_pos(m_stale_label, UI_SCREEN_W - 95, 12);
    lv_obj_set_size(m_stale_label, 85, 22);
    lv_obj_set_style_text_align(m_stale_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_flag(m_stale_label, LV_OBJ_FLAG_HIDDEN);

    // 4. Large Title label (guaranteed non-overlapping bounding box)
    m_title_label = lv_label_create(m_screen);
    lv_label_set_text(m_title_label, "No Media Playing");
    lv_obj_set_style_text_color(m_title_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_title_label, UI_FONT_XL, 0);
    lv_obj_set_pos(m_title_label, 185, 42);
    lv_obj_set_size(m_title_label, 290, 34);
    lv_obj_set_style_text_align(m_title_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(m_title_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR); // Marquee only when the title overflows

    // 5. Dedicated Artist label
    m_artist_label = lv_label_create(m_screen);
    lv_label_set_text(m_artist_label, "");
    lv_obj_set_style_text_color(m_artist_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(m_artist_label, UI_FONT_LG, 0);
    lv_obj_set_pos(m_artist_label, 185, 80);
    lv_obj_set_size(m_artist_label, 290, 26);
    lv_obj_set_style_text_align(m_artist_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(m_artist_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR); // Marquee only when the artist overflows

    // 6. Scrubber bar
    m_progress_bar = lv_bar_create(m_screen);
    lv_obj_set_size(m_progress_bar, 290, 18);
    lv_obj_set_pos(m_progress_bar, 185, 116);
    lv_bar_set_range(m_progress_bar, 0, 100);
    lv_bar_set_value(m_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(m_progress_bar, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_bg_color(m_progress_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(m_progress_bar, 5, 0);
    lv_obj_set_style_radius(m_progress_bar, 5, LV_PART_INDICATOR);
    lv_obj_set_ext_click_area(m_progress_bar, 20);
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
    m_time_label = nullptr;

    // 7. Transport buttons (Prev, Play/Pause, Next) centered across 290 px column (x ≈ 190, 270, 360)
    struct CtrlSpec { int32_t x, w; const char *symbol; int id; };
    const CtrlSpec ctrls[3] = {
        {190, 72, LV_SYMBOL_PREV, BTN_PREV},
        {274, 76, LV_SYMBOL_PLAY, BTN_PLAY_PAUSE},
        {362, 72, LV_SYMBOL_NEXT, BTN_NEXT},
    };

    for (const CtrlSpec &spec : ctrls) {
        lv_obj_t *btn = lv_button_create(m_screen);
        lv_obj_set_size(btn, spec.w, 52);
        lv_obj_set_pos(btn, spec.x, 158);
        lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE2, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, UI_COLOR_SURFACE2, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, spec.id == BTN_PLAY_PAUSE ? 26 : 10, 0);
        lv_obj_set_style_pad_all(btn, 2, 0);
        lv_obj_set_ext_click_area(btn, 10);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, spec.symbol);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, UI_FONT_LG, 0);
        lv_obj_center(lbl);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_set_user_data(btn, (void*)(intptr_t)spec.id);
        lv_obj_add_event_cb(btn, onTransportClicked, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(btn, onTransportClicked, LV_EVENT_SHORT_CLICKED, this);

        if (spec.id == BTN_PREV) m_btn_prev = btn;
        else if (spec.id == BTN_PLAY_PAUSE) m_btn_play_pause = btn;
        else if (spec.id == BTN_NEXT) m_btn_next = btn;
    }

    lv_obj_set_style_bg_color(m_btn_play_pause, UI_COLOR_ACCENT, 0);
    m_play_pause_label = lv_obj_get_child(m_btn_play_pause, 0);
    if (m_play_pause_label) {
        lv_obj_set_style_text_color(m_play_pause_label, UI_COLOR_BG, 0);
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
    // High-contrast cockpit theme: m_screen background remains permanently UI_COLOR_BG.
    // Only update discrete interactive and metadata widget accents (zero background invalidation):
    lv_obj_set_style_border_color(m_artwork_box, lv_color_hex(m_accent_rgb), 0);
    lv_obj_set_style_bg_color(m_progress_bar, lv_color_hex(m_accent_ui_rgb),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(m_btn_play_pause, lv_color_hex(m_accent_ui_rgb), 0);
    lv_obj_set_style_text_color(m_app_label, lv_color_hex(m_accent_ui_rgb), 0);
    if (m_artwork_icon) {
        lv_obj_set_style_text_color(m_artwork_icon, lv_color_hex(m_accent_ui_rgb), 0);
    }
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
    // no title, scrubber fill or artwork from the finished session survives.
    if (state.status == dashcue_v1_PlaybackStatus_PB_NO_SESSION ||
        state.status == dashcue_v1_PlaybackStatus_PB_STOPPED) {
        teardownMedia();
        return;
    }

    lv_obj_remove_state(m_btn_play_pause, LV_STATE_DISABLED);

    if (state.app_name[0]) {
        snprintf(m_last_app, sizeof(m_last_app), "%s", state.app_name);
        lv_label_set_text(m_app_label, state.app_name);
    }

    // Track change: evict the previous cover *before* the new chunks decode, so
    // the HUD can never show track A's art while track B is playing. Title and
    // artist are the identity here (snapshot_version also bumps on pause/seek).
    if (strcmp(state.title, m_last_title) != 0 || strcmp(state.artist, m_last_artist) != 0) {
        snprintf(m_last_title, sizeof(m_last_title), "%s", state.title);
        snprintf(m_last_artist, sizeof(m_last_artist), "%s", state.artist);
        // Keep the cover when it already belongs to this track: artwork and
        // MediaState cross BLE independently, so the transfer can land before
        // the state that names its id. Invalidating unconditionally made that
        // ordering blank the tile until the next resend.
        if (!artwork_store_valid_for(state.artwork_id)) {
            artwork_store_invalidate();
        }
        // Paint the fallback glyph now rather than waiting for the next tick.
        if (m_artwork_img) lv_obj_add_flag(m_artwork_img, LV_OBJ_FLAG_HIDDEN);
        if (m_artwork_icon) lv_obj_remove_flag(m_artwork_icon, LV_OBJ_FLAG_HIDDEN);
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

    // A missing session is not a stale one: with no player there is nothing to
    // update, so the amber badge would be permanently wrong.
    const bool has_session = state.status != dashcue_v1_PlaybackStatus_PB_NO_SESSION;
    m_stale = is_stale && has_session;
    if (m_stale) {
        lv_obj_remove_flag(m_stale_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(m_stale_label, LV_OBJ_FLAG_HIDDEN);
    }

    // Artwork and MediaState travel as separate BLE messages, so the cover can
    // finish before (or after) the snapshot that names its artwork_id. Painting
    // only from IPC_EVT_ARTWORK_READY missed the "state arrived last" order and
    // left the fallback glyph on a completed transfer. refreshArtwork() is
    // generation-guarded, so calling it on every snapshot is free when nothing
    // changed.
    refreshArtwork();
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

void UiScreenMedia::refreshArtwork() {
    if (!m_artwork_img || !m_artwork_icon) return;
    dashcue_v1_MediaState cur_state;
    app_state_get_media_snapshot(&cur_state);

    if (!artwork_store_valid_for(cur_state.artwork_id)) {
        lv_obj_add_flag(m_artwork_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(m_artwork_icon, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const uint32_t generation = artwork_store_generation();
    if (generation == m_artwork_generation) return;

    // Rebuild the descriptor from scratch every generation change.
    // header.flags was previously left uninitialized; after a screen rebuild
    // (*this = UiScreenMedia()) it held stack garbage, and decoder-visible
    // bits like LV_IMAGE_FLAGS_COMPRESSED (0x0008) or LV_IMAGE_FLAGS_ALLOCATED
    // (0x0010) sent LVGL's bin decoder down a decompress/free path — a
    // NULL-buffer read in the transform and a free() of this static buffer,
    // i.e. heap corruption. Measured crash on hardware 2026-10-26 (Guru
    // Meditation StoreProhibited).
    memset(&m_artwork_dsc, 0, sizeof(m_artwork_dsc));
    m_artwork_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    m_artwork_dsc.header.cf = LV_COLOR_FORMAT_RGB565;

    // Antialias is permanently OFF for the scaled artwork.
    // Previously the 64x64 source was scaled at paint time with antialiasing,
    // which allocates + frees a destination-sized transform buffer on EVERY
    // repaint of the artwork region — the progress bar repaints every second,
    // so a ~48 KB alloc/free cycle ran continuously, fragmenting the heap
    // until Bluedroid's HCI reassembler failed its ~1 KB allocation and the
    // BT host StoreProhibited-crashed mid-stream (root-caused and measured
    // 2026-10-26, symbolized packet_fragmenter.c:175 in both soak crashes).
    // With antialias off LVGL does a nearest-neighbour transform with no
    // intermediate buffer. The boot-time bilinear prescale cache was also
    // tried on 2026-10-26 and REVERTED: allocating its 48,672 B before the
    // BT stacks start stole the largest contiguous block, BLE init fell from
    // largest=77,812 to 31,732 and the display stage ended at 9 KB free /
    // 8.6 KB largest — 14 resets in one churn soak. Only callers with a
    // verified >96 KB largest-block budget may enable it.
    const uint32_t w = artwork_store_width();
    const uint32_t h = artwork_store_height();
    const uint32_t valid_w = (w > 0) ? w : ARTWORK_W;
    const uint32_t valid_h = (h > 0) ? h : ARTWORK_H;
    m_artwork_dsc.header.w = valid_w;
    m_artwork_dsc.header.h = valid_h;
    m_artwork_dsc.header.stride = valid_w * 2;
    m_artwork_dsc.data_size = valid_w * valid_h * 2;
    m_artwork_dsc.data = artwork_store_data();
    lv_image_set_scale(m_artwork_img, (ART_INNER * 256) / valid_w);
    lv_image_set_antialias(m_artwork_img, false);
    lv_image_set_src(m_artwork_img, &m_artwork_dsc);
    lv_obj_center(m_artwork_img);
    lv_obj_remove_flag(m_artwork_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_artwork_icon, LV_OBJ_FLAG_HIDDEN);
    m_artwork_generation = generation;

    // Rethemes the backdrop with exact dominant color
    applyAccent(artwork_store_dominant_rgb());
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
    if (m_play_pause_label) lv_label_set_text(m_play_pause_label, LV_SYMBOL_PLAY);
    lv_obj_add_state(m_btn_play_pause, LV_STATE_DISABLED);
    lv_bar_set_value(m_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_add_flag(m_stale_label, LV_OBJ_FLAG_HIDDEN);
    if (m_volume_label) lv_obj_add_flag(m_volume_label, LV_OBJ_FLAG_HIDDEN);
    m_volume_toast_until = 0;

    // Reclaim the artwork immediately (8,192 B) and drop the album tint.
    artwork_store_invalidate();
    if (m_artwork_img) lv_obj_add_flag(m_artwork_img, LV_OBJ_FLAG_HIDDEN);
    if (m_artwork_icon) lv_obj_remove_flag(m_artwork_icon, LV_OBJ_FLAG_HIDDEN);
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
    } else {
        lv_bar_set_value(m_progress_bar, 0, LV_ANIM_OFF);
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
