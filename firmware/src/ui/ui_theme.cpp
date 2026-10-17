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

void ui_theme_init(void) {
    static bool s_initialized = false;
    if (s_initialized) return;
    s_initialized = true;

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
