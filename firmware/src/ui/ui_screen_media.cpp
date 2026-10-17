#include "ui_screen_media.h"
#include "ui_theme.h"
#include "artwork_store.h"
#include "state/ipc_queues.h"
#include "ui_command_id.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

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
static const int32_t ART_TILE = 120;
// The image sits inside the 2 px border, so it is 116 px on a 120 px tile.
static const int32_t ART_INNER = ART_TILE - ART_BORDER * 2; // 116
static const int32_t ART_X = 10;
static const int32_t ART_Y = 8;

// Linear RGB interpolation used by the backdrop crossfade. Done in 8-bit
// sRGB space on purpose: the interpolation is 300 ms of a background fill, and
// a gamma-correct lerp is not worth the per-frame pow() on a CYD.
static inline uint32_t lerp_rgb(uint32_t a, uint32_t b, uint32_t t) {
    const uint32_t inv = 255 - t;
    const uint32_t r = (((a >> 16) & 0xFF) * inv + ((b >> 16) & 0xFF) * t) / 255;
    const uint32_t g = (((a >> 8) & 0xFF) * inv + ((b >> 8) & 0xFF) * t) / 255;
    const uint32_t bl = ((a & 0xFF) * inv + (b & 0xFF) * t) / 255;
    return (r << 16) | (g << 8) | bl;
}

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

// Spotify-style gradient: top is 52% of the extracted dominant accent for high-contrast backdrop.
static inline uint32_t darken_for_gradient_top(uint32_t rgb) {
    return ((((rgb >> 16) & 0xFF) * 52 / 100) << 16) |
           ((((rgb >> 8) & 0xFF) * 52 / 100) << 8) |
           ((rgb & 0xFF) * 52 / 100);
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
      m_volume_pct(0),
      m_muted(false),
      m_stale(true),
      m_accent_rgb(0),
      m_accent_ui_rgb(0),
      m_grad_top_rgb(0),
      m_anim_raw_from(0),
      m_anim_raw_to(0),
      m_anim_top_from(0),
      m_anim_top_to(0),
      m_anim_ui_from(0),
      m_anim_ui_to(0),
      m_anim_active(false),
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
    lv_obj_set_style_border_width(m_screen, 0, 0);
    lv_obj_set_style_radius(m_screen, 0, 0);
    lv_obj_set_style_pad_all(m_screen, 0, 0);
    lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_SCROLLABLE);
    // The vertical album gradient is installed by applyAccent() below.

    // 1. Hero album art, left-aligned (120x120 tile).
    m_artwork_box = lv_obj_create(m_screen);
    lv_obj_set_size(m_artwork_box, ART_TILE, ART_TILE);
    lv_obj_set_pos(m_artwork_box, ART_X, ART_Y);
    lv_obj_set_style_bg_color(m_artwork_box, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(m_artwork_box, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(m_artwork_box, ART_BORDER, 0);
    lv_obj_set_style_radius(m_artwork_box, 10, 0);
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
    // 256 = 1x. 116/64 * 256 = 464 for bilinear filtered upscale
    lv_image_set_scale(m_artwork_img, (ART_INNER * 256) / ARTWORK_W);
    lv_image_set_antialias(m_artwork_img, true);
    lv_obj_add_flag(m_artwork_img, LV_OBJ_FLAG_HIDDEN);

    // 2. Volume controls under artwork ([Vol-] and [Vol+], 56x34 with ext click hitbox)
    lv_obj_t *btn_vd = lv_button_create(m_screen);
    lv_obj_set_size(btn_vd, 56, 34);
    lv_obj_set_pos(btn_vd, 10, 132);
    lv_obj_set_style_bg_color(btn_vd, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_color(btn_vd, UI_COLOR_SURFACE2, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_vd, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_border_width(btn_vd, 1, 0);
    lv_obj_set_style_radius(btn_vd, 6, 0);
    lv_obj_set_ext_click_area(btn_vd, 6);
    lv_obj_set_user_data(btn_vd, (void*)(intptr_t)BTN_VOL_DOWN);
    lv_obj_add_event_cb(btn_vd, onTransportClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *lbl_vd = lv_label_create(btn_vd);
    lv_label_set_text(lbl_vd, LV_SYMBOL_VOLUME_MID);
    lv_obj_set_style_text_color(lbl_vd, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_vd, UI_FONT_MD, 0);
    lv_obj_center(lbl_vd);

    lv_obj_t *btn_vu = lv_button_create(m_screen);
    lv_obj_set_size(btn_vu, 56, 34);
    lv_obj_set_pos(btn_vu, 74, 132);
    lv_obj_set_style_bg_color(btn_vu, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_color(btn_vu, UI_COLOR_SURFACE2, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_vu, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_border_width(btn_vu, 1, 0);
    lv_obj_set_style_radius(btn_vu, 6, 0);
    lv_obj_set_ext_click_area(btn_vu, 6);
    lv_obj_set_user_data(btn_vu, (void*)(intptr_t)BTN_VOL_UP);
    lv_obj_add_event_cb(btn_vu, onTransportClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *lbl_vu = lv_label_create(btn_vu);
    lv_label_set_text(lbl_vu, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(lbl_vu, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_vu, UI_FONT_MD, 0);
    lv_obj_center(lbl_vu);

    // 3. Right column: App badge + Stale pill
    m_app_label = lv_label_create(m_screen);
    lv_label_set_text(m_app_label, "DashCue");
    lv_obj_set_style_text_color(m_app_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(m_app_label, UI_FONT_SM, 0);
    lv_obj_set_style_bg_color(m_app_label, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(m_app_label, LV_OPA_70, 0);
    lv_obj_set_style_radius(m_app_label, 4, 0);
    lv_obj_set_style_pad_hor(m_app_label, 6, 0);
    lv_obj_set_style_pad_ver(m_app_label, 2, 0);
    lv_obj_set_pos(m_app_label, 138, 8);
    lv_obj_set_width(m_app_label, 96);
    lv_label_set_long_mode(m_app_label, LV_LABEL_LONG_MODE_DOTS);

    m_stale_label = lv_label_create(m_screen);
    lv_label_set_text(m_stale_label, LV_SYMBOL_WARNING " STALE");
    lv_obj_set_style_text_color(m_stale_label, UI_COLOR_WARN, 0);
    lv_obj_set_style_text_font(m_stale_label, UI_FONT_SM, 0);
    lv_obj_set_style_bg_color(m_stale_label, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(m_stale_label, LV_OPA_70, 0);
    lv_obj_set_style_radius(m_stale_label, 4, 0);
    lv_obj_set_style_pad_hor(m_stale_label, 6, 0);
    lv_obj_set_style_pad_ver(m_stale_label, 2, 0);
    lv_obj_set_pos(m_stale_label, UI_SCREEN_W - 78, 8);
    lv_obj_set_width(m_stale_label, 70);
    lv_obj_set_style_text_align(m_stale_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_flag(m_stale_label, LV_OBJ_FLAG_HIDDEN);

    // 4. Large Title label
    m_title_label = lv_label_create(m_screen);
    lv_label_set_text(m_title_label, "No Media Playing");
    lv_obj_set_style_text_color(m_title_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_title_label, UI_FONT_LG, 0);
    lv_obj_set_pos(m_title_label, 138, 30);
    lv_obj_set_size(m_title_label, UI_SCREEN_W - 146, 22);
    lv_obj_set_style_text_align(m_title_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(m_title_label, LV_LABEL_LONG_MODE_DOTS);

    // 5. Dedicated Artist label
    m_artist_label = lv_label_create(m_screen);
    lv_label_set_text(m_artist_label, "");
    lv_obj_set_style_text_color(m_artist_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(m_artist_label, UI_FONT_MD, 0);
    lv_obj_set_pos(m_artist_label, 138, 54);
    lv_obj_set_size(m_artist_label, UI_SCREEN_W - 146, 20);
    lv_obj_set_style_text_align(m_artist_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(m_artist_label, LV_LABEL_LONG_MODE_DOTS);

    // 6. Scrubber bar
    m_progress_bar = lv_bar_create(m_screen);
    lv_obj_set_size(m_progress_bar, UI_SCREEN_W - 146, 12);
    lv_obj_set_pos(m_progress_bar, 138, 78);
    lv_bar_set_range(m_progress_bar, 0, 100);
    lv_bar_set_value(m_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(m_progress_bar, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_bg_color(m_progress_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(m_progress_bar, 3, 0);
    lv_obj_set_style_radius(m_progress_bar, 3, LV_PART_INDICATOR);
    lv_obj_set_ext_click_area(m_progress_bar, 16);
    lv_obj_add_event_cb(m_progress_bar, onProgressEvent, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(m_progress_bar, onProgressEvent, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(m_progress_bar, onProgressEvent, LV_EVENT_RELEASED, this);
    m_time_label = nullptr;

    // 7. Transport buttons (Prev, Play/Pause, Next)
    struct CtrlSpec { int32_t x, w; const char *symbol; int id; };
    const CtrlSpec ctrls[3] = {
        {138, 50, LV_SYMBOL_PREV, BTN_PREV},
        {196, 58, LV_SYMBOL_PLAY, BTN_PLAY_PAUSE},
        {260, 50, LV_SYMBOL_NEXT, BTN_NEXT},
    };

    for (const CtrlSpec &spec : ctrls) {
        lv_obj_t *btn = lv_button_create(m_screen);
        lv_obj_set_size(btn, spec.w, 48);
        lv_obj_set_pos(btn, spec.x, 104);
        lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE2, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, UI_COLOR_SURFACE2, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, spec.id == BTN_PLAY_PAUSE ? 24 : 8, 0);
        lv_obj_set_style_pad_all(btn, 2, 0);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, spec.symbol);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, UI_FONT_LG, 0);
        lv_obj_center(lbl);

        lv_obj_set_user_data(btn, (void*)(intptr_t)spec.id);
        lv_obj_add_event_cb(btn, onTransportClicked, LV_EVENT_CLICKED, this);

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
    // Kill any in-flight gradient fade before the object it paints is freed.
    lv_anim_delete(this, onAccentAnim);
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
    if (!m_screen) {
        m_accent_rgb = rgb;
        return;
    }

    // rgb == 0 means no artwork (or a fully grey image): fall back to the
    // universal per-app brand color or Polestar amber accent so the backdrop is always themed.
    const uint32_t base = rgb ? rgb : fallback_app_color(m_last_app);
    const uint32_t top = darken_for_gradient_top(base);
    const uint32_t ui = lift_for_dark(base);

    // The fade target never changes, so set it once. A vertical gradient is one
    // fill pass; adding stops would add passes on a 40 MHz SPI panel.
    lv_obj_set_style_bg_grad_color(m_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_grad_dir(m_screen, LV_GRAD_DIR_VER, 0);

    // A running fade would fight this one and start from a stale colour.
    lv_anim_delete(this, onAccentAnim);

    if (!animate || m_grad_top_rgb == 0 || top == m_grad_top_rgb) {
        m_anim_raw_to = base;
        m_anim_top_to = top;
        m_anim_ui_to = ui;
        m_anim_active = false;
        onAccentAnim(this, 255);
        return;
    }

    m_anim_raw_from = m_accent_rgb;
    m_anim_raw_to = base;
    m_anim_top_from = m_grad_top_rgb;
    m_anim_top_to = top;
    m_anim_ui_from = m_accent_ui_rgb;
    m_anim_ui_to = ui;
    m_anim_active = true;

    // 300 ms ease-in-out. This repaints the whole 320x168 workspace each frame,
    // so it is deliberately bounded: it only runs on a track change, and the
    // chrome outside this screen snaps rather than fading to stay inside the
    // SPI budget.
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, onAccentAnim);
    lv_anim_start(&a);
}

void UiScreenMedia::paintAccent() {
    if (!m_screen) return;
    lv_obj_set_style_bg_color(m_screen, lv_color_hex(m_grad_top_rgb), 0);
    lv_obj_set_style_border_color(m_artwork_box, lv_color_hex(m_accent_rgb), 0);
    lv_obj_set_style_bg_color(m_progress_bar, lv_color_hex(m_accent_ui_rgb),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(m_btn_play_pause, lv_color_hex(m_accent_ui_rgb), 0);
    lv_obj_set_style_text_color(m_app_label, lv_color_hex(m_accent_ui_rgb), 0);
}

void UiScreenMedia::onAccentAnim(void *var, int32_t value) {
    UiScreenMedia *self = (UiScreenMedia*)var;
    if (!self || !self->m_screen) return;

    if (value >= 255) {
        self->m_accent_rgb = self->m_anim_raw_to;
        self->m_grad_top_rgb = self->m_anim_top_to;
        self->m_accent_ui_rgb = self->m_anim_ui_to;
        self->m_anim_active = false;
    } else {
        const uint32_t t = (uint32_t)value;
        self->m_accent_rgb = lerp_rgb(self->m_anim_raw_from, self->m_anim_raw_to, t);
        self->m_grad_top_rgb = lerp_rgb(self->m_anim_top_from, self->m_anim_top_to, t);
        self->m_accent_ui_rgb = lerp_rgb(self->m_anim_ui_from, self->m_anim_ui_to, t);
    }
    self->paintAccent();

    // Retint the chrome once the fade settles, not on every frame: animating
    // the status bar and tab bar too would exceed the frame budget.
    if (value >= 255 && self->m_accent_listener) {
        self->m_accent_listener(self->m_accent_ui_rgb, self->m_accent_ctx);
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

    // Track change: evict the previous cover *before* the new chunks decode, so
    // the HUD can never show track A's art while track B is playing. Title and
    // artist are the identity here (snapshot_version also bumps on pause/seek).
    if (strcmp(state.title, m_last_title) != 0 || strcmp(state.artist, m_last_artist) != 0) {
        snprintf(m_last_title, sizeof(m_last_title), "%s", state.title);
        snprintf(m_last_artist, sizeof(m_last_artist), "%s", state.artist);
        artwork_store_invalidate();
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

    if (state.app_name[0]) {
        snprintf(m_last_app, sizeof(m_last_app), "%s", state.app_name);
        lv_label_set_text(m_app_label, state.app_name);
    }

    m_is_playing = (state.status == dashcue_v1_PlaybackStatus_PB_PLAYING);
    if (m_play_pause_label) {
        lv_label_set_text(m_play_pause_label, m_is_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }

    m_duration_ms = state.duration_ms;
    m_position_ms = state.position_ms;
    m_playback_speed = (m_is_playing && state.playback_speed > 0.0f) ? state.playback_speed : 0.0f;
    m_position_at_update_ms = millis();
    // The phone rejects a command whose snapshot_version is not the one it
    // issued; capture the version carried by this MediaState.
    m_snapshot_version = state.snapshot_version;

    m_volume_pct = state.volume_pct > 100 ? 100 : state.volume_pct;
    m_muted = state.muted;
    renderProgress();
    renderVolume();

    // A missing session is not a stale one: with no player there is nothing to
    // update, so the amber badge would be permanently wrong.
    const bool has_session = state.status != dashcue_v1_PlaybackStatus_PB_NO_SESSION;
    if (is_stale && has_session) {
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

void UiScreenMedia::refreshArtwork() {
    if (!m_artwork_img || !m_artwork_icon) return;
    if (!artwork_store_valid()) {
        lv_obj_add_flag(m_artwork_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(m_artwork_icon, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const uint32_t generation = artwork_store_generation();
    if (generation == m_artwork_generation) return;

    const uint32_t w = artwork_store_width();
    const uint32_t h = artwork_store_height();
    const uint32_t valid_w = (w > 0) ? w : ARTWORK_W;
    const uint32_t valid_h = (h > 0) ? h : ARTWORK_H;

    m_artwork_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    m_artwork_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    m_artwork_dsc.header.w = valid_w;
    m_artwork_dsc.header.h = valid_h;
    m_artwork_dsc.header.stride = valid_w * 2;
    m_artwork_dsc.data_size = valid_w * valid_h * 2;
    m_artwork_dsc.data = artwork_store_data();

    // Scale dynamically to fill ART_INNER (116 px) for both 64x64 and 32x32 fallback
    lv_image_set_scale(m_artwork_img, (ART_INNER * 256) / valid_w);
    lv_image_set_src(m_artwork_img, &m_artwork_dsc);
    lv_obj_remove_flag(m_artwork_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_artwork_icon, LV_OBJ_FLAG_HIDDEN);
    m_artwork_generation = generation;

    // Rethemes the backdrop with exact dominant color
    applyAccent(artwork_store_dominant_rgb());
}

void UiScreenMedia::tick(uint32_t now_ms) {
    if (!m_screen) return;

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

void UiScreenMedia::onTransportClicked(lv_event_t *e) {
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (now - s_last_touch_ms < 280) return;
    s_last_touch_ms = now;

    UiScreenMedia *self = (UiScreenMedia*)lv_event_get_user_data(e);
    lv_obj_t *target = (lv_obj_t*)lv_event_get_target(e);
    int btn_id = (int)(intptr_t)lv_obj_get_user_data(target);

    if (!self) return;

    UiToProtoMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_CMD_MEDIA;
    msg.data.media.command_id = ui_next_command_id();
    msg.data.media.snapshot_version = self->m_snapshot_version;

    switch (btn_id) {
        case BTN_VOL_DOWN:
            msg.data.media.action = dashcue_v1_MediaAction_MA_VOL_STEP;
            msg.data.media.arg = -1;
            // Optimistic 5% step so the driver gets immediate feedback; the
            // phone's authoritative volume_pct overwrites it on the next update.
            if (!self->m_muted && self->m_volume_pct >= 5) self->m_volume_pct -= 5;
            self->showVolumeToast();
            break;

        case BTN_VOL_UP:
            msg.data.media.action = dashcue_v1_MediaAction_MA_VOL_STEP;
            msg.data.media.arg = 1;
            if (self->m_volume_pct <= 95) self->m_volume_pct += 5; else self->m_volume_pct = 100;
            self->m_muted = false;
            self->showVolumeToast();
            break;

        case BTN_PREV:
            msg.data.media.action = dashcue_v1_MediaAction_MA_PREV;
            break;

        case BTN_PLAY_PAUSE:
            // Explicit action dispatch (DEC-13)
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
}

void UiScreenMedia::onProgressEvent(lv_event_t *e) {
    UiScreenMedia *self = (UiScreenMedia*)lv_event_get_user_data(e);
    if (!self || self->m_duration_ms <= 0) return;

    const lv_event_code_t code = lv_event_get_code(e);
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);

    lv_area_t bar_coords;
    lv_obj_get_coords(self->m_progress_bar, &bar_coords);
    const int32_t width = bar_coords.x2 - bar_coords.x1;
    if (width <= 0) return;

    int32_t rel_x = point.x - bar_coords.x1;
    if (rel_x < 0) rel_x = 0;
    if (rel_x > width) rel_x = width;

    const int64_t target_ms = (rel_x * self->m_duration_ms) / width;

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        // Optimistic preview only; the phone remains the source of truth.
        self->m_scrubbing = true;
        self->m_position_ms = target_ms;
        int32_t pct = (int32_t)((target_ms * 100) / self->m_duration_ms);
        lv_bar_set_value(self->m_progress_bar, pct, LV_ANIM_OFF);
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        self->m_scrubbing = false;
        UiToProtoMsg msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = IPC_CMD_MEDIA;
        msg.data.media.command_id = ui_next_command_id();
        msg.data.media.snapshot_version = self->m_snapshot_version;
        msg.data.media.action = dashcue_v1_MediaAction_MA_SEEK_ABS;
        msg.data.media.arg = target_ms;
        ipc_send_ui_to_proto(&msg, 0);
    }
}
