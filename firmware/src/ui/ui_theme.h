#ifndef DASHCUE_UI_THEME_H
#define DASHCUE_UI_THEME_H

#include <lvgl.h>
#include "dashcue_config.h"

// Spec §12.10 Polestar Scandinavian cockpit tokens, verified by
// contrast_check.py. "Building from Black": near-black base, grayscale
// hierarchy, one warm brand accent (Swedish Amber) reserved for the primary
// action, plus a cool Glacier-Blue alt for status/telemetry.
extern lv_color_t ui_color_bg;
extern lv_color_t ui_color_surface;
extern lv_color_t ui_color_surface2;
extern lv_color_t ui_color_border;
extern lv_color_t ui_color_text;
extern lv_color_t ui_color_text_dim;
extern lv_color_t ui_color_accent;
extern lv_color_t ui_color_accent_alt;
extern lv_color_t ui_color_ok;
extern lv_color_t ui_color_warn;
extern lv_color_t ui_color_danger;

#define UI_COLOR_BG         ui_color_bg
#define UI_COLOR_SURFACE    ui_color_surface
#define UI_COLOR_SURFACE2   ui_color_surface2
#define UI_COLOR_BORDER     ui_color_border
#define UI_COLOR_TEXT       ui_color_text
#define UI_COLOR_TEXT_DIM   ui_color_text_dim
#define UI_COLOR_ACCENT     ui_color_accent
#define UI_COLOR_ACCENT_ALT ui_color_accent_alt
#define UI_COLOR_OK         ui_color_ok
#define UI_COLOR_WARN       ui_color_warn
#define UI_COLOR_DANGER     ui_color_danger

void ui_theme_set_dark(bool dark);
bool ui_theme_is_dark(void);
uint32_t ui_theme_get_color_bg_hex(void);

// Standard typography mappings
#define UI_FONT_SM          (&lv_font_montserrat_12)
#define UI_FONT_MD          (&lv_font_montserrat_14)
#define UI_FONT_LG          (&lv_font_montserrat_16)
#define UI_FONT_XL          (&lv_font_montserrat_20)
#define UI_FONT_TITLE       (&lv_font_montserrat_28)
#define UI_FONT_DIGITS_48   (&lv_font_montserrat_48)

// Geometry
#define UI_SCREEN_W         320
#define UI_SCREEN_H         240
#define UI_STATUS_BAR_H     24
#define UI_TAB_BAR_H        48
#define UI_WORKSPACE_Y      24
#define UI_WORKSPACE_H      (UI_SCREEN_H - UI_STATUS_BAR_H - UI_TAB_BAR_H) // 168

// Shared LVGL styles (§16 heap optimization: eliminate per-object style prop heap allocations)
void ui_theme_init(void);

extern lv_style_t ui_style_screen;
extern lv_style_t ui_style_card;
extern lv_style_t ui_style_row;
extern lv_style_t ui_style_btn_secondary;
extern lv_style_t ui_style_btn_accent;
extern lv_style_t ui_style_btn_warn;
extern lv_style_t ui_style_btn_danger;
extern lv_style_t ui_style_disabled;
extern lv_style_t ui_style_text_dim_sm;
extern lv_style_t ui_style_text_dim_md;
extern lv_style_t ui_style_text_md;
extern lv_style_t ui_style_text_accent_sm;
extern lv_style_t ui_style_text_accent_md;

#endif // DASHCUE_UI_THEME_H
