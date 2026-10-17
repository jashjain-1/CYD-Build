#include "ui_theme.h"

lv_style_t ui_style_screen;
lv_style_t ui_style_card;
lv_style_t ui_style_row;
lv_style_t ui_style_btn_secondary;
lv_style_t ui_style_btn_accent;
lv_style_t ui_style_btn_warn;
lv_style_t ui_style_btn_danger;
lv_style_t ui_style_disabled;
lv_style_t ui_style_text_dim_sm;
lv_style_t ui_style_text_dim_md;
lv_style_t ui_style_text_md;
lv_style_t ui_style_text_accent_sm;
lv_style_t ui_style_text_accent_md;

lv_color_t ui_color_bg;
lv_color_t ui_color_surface;
lv_color_t ui_color_surface2;
lv_color_t ui_color_border;
lv_color_t ui_color_text;
lv_color_t ui_color_text_dim;
lv_color_t ui_color_accent;
lv_color_t ui_color_accent_alt;
lv_color_t ui_color_ok;
lv_color_t ui_color_warn;
lv_color_t ui_color_danger;

static bool s_is_dark = true;

static void update_theme_colors(bool dark) {
    s_is_dark = dark;
    if (dark) {
        ui_color_bg         = lv_color_hex(0x050608);
        ui_color_surface    = lv_color_hex(0x111317);
        ui_color_surface2   = lv_color_hex(0x1A1D22);
        ui_color_border     = lv_color_hex(0x242831);
        ui_color_text       = lv_color_hex(0xF3F4F6);
        ui_color_text_dim   = lv_color_hex(0x848B98);
        ui_color_accent     = lv_color_hex(0xE58B42);
        ui_color_accent_alt = lv_color_hex(0x38BDF8);
        ui_color_ok         = lv_color_hex(0x10B981);
        ui_color_warn       = lv_color_hex(0xF59E0B);
        ui_color_danger     = lv_color_hex(0xEF4444);
    } else {
        ui_color_bg         = lv_color_hex(0xF8FAFC);
        ui_color_surface    = lv_color_hex(0xE2E8F0);
        ui_color_surface2   = lv_color_hex(0xCBD5E1);
        ui_color_border     = lv_color_hex(0x94A3B8);
        ui_color_text       = lv_color_hex(0x0B1220);
        ui_color_text_dim   = lv_color_hex(0x334155);
        ui_color_accent     = lv_color_hex(0x0369A1);
        ui_color_accent_alt = lv_color_hex(0x0284C7);
        ui_color_ok         = lv_color_hex(0x047857);
        ui_color_warn       = lv_color_hex(0xB45309);
        ui_color_danger     = lv_color_hex(0xB91C1C);
    }
}

void ui_theme_set_dark(bool dark) {
    update_theme_colors(dark);

    // Update style colors
    lv_style_set_bg_color(&ui_style_screen, ui_color_bg);

    lv_style_set_bg_color(&ui_style_card, ui_color_surface);
    lv_style_set_border_color(&ui_style_card, ui_color_surface2);

    lv_style_set_bg_color(&ui_style_row, ui_color_surface);
    lv_style_set_border_color(&ui_style_row, ui_color_surface2);

    lv_style_set_bg_color(&ui_style_btn_secondary, ui_color_surface2);
    lv_style_set_bg_color(&ui_style_btn_accent, ui_color_accent);
    lv_style_set_bg_color(&ui_style_btn_warn, ui_color_warn);
    lv_style_set_bg_color(&ui_style_btn_danger, ui_color_danger);

    lv_style_set_text_color(&ui_style_text_dim_sm, ui_color_text_dim);
    lv_style_set_text_color(&ui_style_text_dim_md, ui_color_text_dim);
    lv_style_set_text_color(&ui_style_text_md, ui_color_text);
    lv_style_set_text_color(&ui_style_text_accent_sm, ui_color_accent);
    lv_style_set_text_color(&ui_style_text_accent_md, ui_color_accent);

    // Report style changes to notify LVGL
    lv_obj_report_style_change(&ui_style_screen);
    lv_obj_report_style_change(&ui_style_card);
    lv_obj_report_style_change(&ui_style_row);
    lv_obj_report_style_change(&ui_style_btn_secondary);
    lv_obj_report_style_change(&ui_style_btn_accent);
    lv_obj_report_style_change(&ui_style_btn_warn);
    lv_obj_report_style_change(&ui_style_btn_danger);
    lv_obj_report_style_change(&ui_style_text_dim_sm);
    lv_obj_report_style_change(&ui_style_text_dim_md);
    lv_obj_report_style_change(&ui_style_text_md);
    lv_obj_report_style_change(&ui_style_text_accent_sm);
    lv_obj_report_style_change(&ui_style_text_accent_md);
}

bool ui_theme_is_dark(void) {
    return s_is_dark;
}

uint32_t ui_theme_get_color_bg_hex(void) {
    return s_is_dark ? 0x050608 : 0xF8FAFC;
}

void ui_theme_init(void) {
    static bool s_initialized = false;
    if (s_initialized) return;
    s_initialized = true;

    update_theme_colors(true);

    // Screen base container
    lv_style_init(&ui_style_screen);
    lv_style_set_bg_color(&ui_style_screen, UI_COLOR_BG);
    lv_style_set_bg_opa(&ui_style_screen, LV_OPA_COVER);
    lv_style_set_border_width(&ui_style_screen, 0);
    lv_style_set_radius(&ui_style_screen, 0);
    lv_style_set_pad_all(&ui_style_screen, 0);

    // Card container (dialogs, cards, canvases)
    lv_style_init(&ui_style_card);
    lv_style_set_bg_color(&ui_style_card, UI_COLOR_SURFACE);
    lv_style_set_border_color(&ui_style_card, UI_COLOR_SURFACE2);
    lv_style_set_border_width(&ui_style_card, 1);
    lv_style_set_radius(&ui_style_card, 8);
    lv_style_set_pad_all(&ui_style_card, 8);

    // Row container (settings list rows)
    lv_style_init(&ui_style_row);
    lv_style_set_bg_color(&ui_style_row, UI_COLOR_SURFACE);
    lv_style_set_border_color(&ui_style_row, UI_COLOR_SURFACE2);
    lv_style_set_border_width(&ui_style_row, 1);
    lv_style_set_radius(&ui_style_row, 6);
    lv_style_set_pad_all(&ui_style_row, 6);

    // Button secondary (SURFACE2 background)
    lv_style_init(&ui_style_btn_secondary);
    lv_style_set_bg_color(&ui_style_btn_secondary, UI_COLOR_SURFACE2);
    lv_style_set_border_width(&ui_style_btn_secondary, 0);
    lv_style_set_radius(&ui_style_btn_secondary, 6);

    // Button accent (cyan primary)
    lv_style_init(&ui_style_btn_accent);
    lv_style_set_bg_color(&ui_style_btn_accent, UI_COLOR_ACCENT);
    lv_style_set_border_width(&ui_style_btn_accent, 0);
    lv_style_set_radius(&ui_style_btn_accent, 6);

    // Button warning
    lv_style_init(&ui_style_btn_warn);
    lv_style_set_bg_color(&ui_style_btn_warn, UI_COLOR_WARN);
    lv_style_set_border_width(&ui_style_btn_warn, 0);
    lv_style_set_radius(&ui_style_btn_warn, 6);

    // Button danger
    lv_style_init(&ui_style_btn_danger);
    lv_style_set_bg_color(&ui_style_btn_danger, UI_COLOR_DANGER);
    lv_style_set_border_width(&ui_style_btn_danger, 0);
    lv_style_set_radius(&ui_style_btn_danger, 6);

    // Disabled state style for buttons (reduces opacity without setting per-object styles)
    lv_style_init(&ui_style_disabled);
    lv_style_set_opa(&ui_style_disabled, LV_OPA_40);

    // Text dim small (12px)
    lv_style_init(&ui_style_text_dim_sm);
    lv_style_set_text_color(&ui_style_text_dim_sm, UI_COLOR_TEXT_DIM);
    lv_style_set_text_font(&ui_style_text_dim_sm, UI_FONT_SM);

    // Text dim medium (14px)
    lv_style_init(&ui_style_text_dim_md);
    lv_style_set_text_color(&ui_style_text_dim_md, UI_COLOR_TEXT_DIM);
    lv_style_set_text_font(&ui_style_text_dim_md, UI_FONT_MD);

    // Text standard medium (14px, bright)
    lv_style_init(&ui_style_text_md);
    lv_style_set_text_color(&ui_style_text_md, UI_COLOR_TEXT);
    lv_style_set_text_font(&ui_style_text_md, UI_FONT_MD);

    // Text accent small (12px, accent cyan)
    lv_style_init(&ui_style_text_accent_sm);
    lv_style_set_text_color(&ui_style_text_accent_sm, UI_COLOR_ACCENT);
    lv_style_set_text_font(&ui_style_text_accent_sm, UI_FONT_SM);

    // Text accent medium (14px, accent cyan)
    lv_style_init(&ui_style_text_accent_md);
    lv_style_set_text_color(&ui_style_text_accent_md, UI_COLOR_ACCENT);
    lv_style_set_text_font(&ui_style_text_accent_md, UI_FONT_MD);
}
