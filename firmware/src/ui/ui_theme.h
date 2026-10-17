#ifndef DASHCUE_UI_THEME_H
#define DASHCUE_UI_THEME_H

#include <lvgl.h>
#include "dashcue_config.h"

// Spec §12.10 Polestar Scandinavian cockpit tokens, verified by
// contrast_check.py. "Building from Black": near-black base, grayscale
// hierarchy, one warm brand accent (Swedish Amber) reserved for the primary
// action, plus a cool Glacier-Blue alt for status/telemetry.
#define UI_COLOR_BG         lv_color_hex(0x050608)
#define UI_COLOR_SURFACE    lv_color_hex(0x111317)
#define UI_COLOR_SURFACE2   lv_color_hex(0x1A1D22)
#define UI_COLOR_BORDER     lv_color_hex(0x242831)
#define UI_COLOR_TEXT       lv_color_hex(0xF3F4F6)
#define UI_COLOR_TEXT_DIM   lv_color_hex(0x848B98)
#define UI_COLOR_ACCENT     lv_color_hex(0xE58B42) // Swedish Amber (primary action)
#define UI_COLOR_ACCENT_ALT lv_color_hex(0x38BDF8) // Glacier Blue (status/telemetry)
#define UI_COLOR_OK         lv_color_hex(0x10B981)
#define UI_COLOR_WARN       lv_color_hex(0xF59E0B)
#define UI_COLOR_DANGER     lv_color_hex(0xEF4444)

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
