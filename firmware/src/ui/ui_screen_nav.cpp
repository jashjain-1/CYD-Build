#include "ui_screen_nav.h"
#include "ui_theme.h"
#include "state/ipc_queues.h"
#include "ui_command_id.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef DASHCUE_NATIVE_TEST
#include <Arduino.h>
#else
static uint32_t millis() { return 0; }
#endif

// ---------------------------------------------------------------------------
// Geometry. The maneuver card is 150x232 at (8,8); the instruction card is
// 310x232 at (162,8). All coordinates below are local to their parent.
// ---------------------------------------------------------------------------
static constexpr int kCompassCx = 75;
static constexpr int kCompassCy = 68;
static constexpr int kCompassR = 60;

// Nav street names can be arbitrarily long. When the one-line title overflows
// the 290 px instruction card, wrap it onto a second line instead of showing a
// truncated "Grand Aven..." ellipsis, and shift the "then <dist>" hint below
// the second line so the two never collide or spill into the info strip.
static void adapt_street_layout(lv_obj_t *street, lv_obj_t *next, const char *text) {
    if (!street) return;
    lv_point_t size;
    lv_text_get_size(&size, text, UI_FONT_TITLE, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const bool wraps = size.x > 290 && size.y > 0 && size.y <= 100;
    if (wraps) {
        lv_label_set_long_mode(street, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_size(street, 290, size.y);
        lv_obj_set_pos(street, 10, 64);
        if (next) lv_obj_set_pos(next, 10, 64 + size.y + 2);
    } else {
        lv_label_set_long_mode(street, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(street, 290, 34);
        lv_obj_set_pos(street, 10, 64);
        if (next) lv_obj_set_pos(next, 10, 102);
    }
}

static const char *cardinal_from_bearing(float deg) {
    while (deg < 0.0f) deg += 360.0f;
    while (deg >= 360.0f) deg -= 360.0f;
    static const char *kCardinals[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    const int idx = (int)((deg + 22.5f) / 45.0f) % 8;
    return kCardinals[idx];
}

UiScreenNav::UiScreenNav()
    : m_screen(nullptr),
      m_maneuver_box(nullptr),
      m_instruction_card(nullptr),
      m_mode(MODE_OFFLINE),
      m_connected(false),
      m_bonded(false),
      m_route_active(false),
      m_has_heading(false),
      m_offline_card(nullptr),
      m_offline_icon(nullptr),
      m_offline_title(nullptr),
      m_offline_body(nullptr),
      m_offline_hint(nullptr),
      m_offline_pair_btn(nullptr),
      m_compass_ring(nullptr),
      m_compass_needle(nullptr),
      m_compass_tail(nullptr),
      m_compass_hub(nullptr),
      m_compass_heading_label(nullptr),
      m_compass_sub_label(nullptr),
      m_standby_header(nullptr),
      m_standby_link_dot(nullptr),
      m_standby_street_label(nullptr),
      m_standby_speed_digits(nullptr),
      m_standby_speed_unit(nullptr),
      m_speed_bar(nullptr),
      m_speed_limit_badge(nullptr),
      m_speed_limit_val(nullptr),
      m_standby_status_label(nullptr),
      m_standby_accuracy_label(nullptr),
      m_standby_maps_btn(nullptr),
      m_arrow_shaft(nullptr),
      m_arrow_head(nullptr),
      m_arrow_flag(nullptr),
      m_roundabout_arc(nullptr),
      m_maneuver_caption(nullptr),
      m_shaft_pt_count(2),
      m_roundabout_badge(nullptr),
      m_roundabout_label(nullptr),
      m_distance_label(nullptr),
      m_street_label(nullptr),
      m_next_street_label(nullptr),
      m_info_strip(nullptr),
      m_eta_label(nullptr),
      m_remaining_dist_label(nullptr),
      m_end_nav_btn(nullptr),
      m_stale_overlay(nullptr),
      m_confirm_modal(nullptr),
      m_btn_modal_cancel(nullptr),
      m_btn_modal_confirm(nullptr),
      m_confirm_token(0),
      m_imperial(false) {
    for (int i = 0; i < 4; i++) {
        m_cardinal_labels[i] = nullptr;
        m_tick_lines[i] = nullptr;
        m_tick_pts[i][0] = {0, 0};
        m_tick_pts[i][1] = {0, 0};
    }
    m_needle_pts[0] = {kCompassCx, kCompassCy};
    m_needle_pts[1] = {kCompassCx, kCompassCy - 40};
    m_tail_pts[0] = {kCompassCx, kCompassCy};
    m_tail_pts[1] = {kCompassCx, kCompassCy + 30};

    m_shaft_pts[0] = {75, 135};
    m_shaft_pts[1] = {75, 32};
    m_head_pts[0] = {48, 60};
    m_head_pts[1] = {75, 30};
    m_head_pts[2] = {102, 60};
    m_flag_pts[0] = {36, 135};
    m_flag_pts[1] = {66, 135};
    m_flag_pts[2] = {51, 135};
    m_flag_pts[3] = {51, 28};
    m_flag_pts[4] = {105, 56};
    m_flag_pts[5] = {51, 84};
}

void UiScreenNav::create(lv_obj_t *parent) {
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

    // -----------------------------------------------------------------------
    // 1. Maneuver card (left). Hosts either the turn arrow (active route) or
    //    the compass rose (standby). Never both: two north arrows on one card
    //    would be read as two different headings.
    // -----------------------------------------------------------------------
    m_maneuver_box = lv_obj_create(m_screen);
    lv_obj_set_size(m_maneuver_box, 150, 232);
    lv_obj_set_pos(m_maneuver_box, 8, 8);
    lv_obj_set_style_bg_color(m_maneuver_box, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(m_maneuver_box, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_border_width(m_maneuver_box, 1, 0);
    lv_obj_set_style_radius(m_maneuver_box, 12, 0);
    lv_obj_set_style_pad_all(m_maneuver_box, 0, 0);
    lv_obj_remove_flag(m_maneuver_box, LV_OBJ_FLAG_SCROLLABLE);

    m_arrow_shaft = lv_line_create(m_maneuver_box);
    lv_obj_set_style_line_width(m_arrow_shaft, 10, 0);
    lv_obj_set_style_line_rounded(m_arrow_shaft, true, 0);
    lv_obj_set_style_line_color(m_arrow_shaft, UI_COLOR_ACCENT, 0);
    lv_line_set_points(m_arrow_shaft, m_shaft_pts, m_shaft_pt_count);
    lv_obj_remove_flag(m_arrow_shaft, LV_OBJ_FLAG_CLICKABLE);

    m_arrow_head = lv_line_create(m_maneuver_box);
    lv_obj_set_style_line_width(m_arrow_head, 10, 0);
    lv_obj_set_style_line_rounded(m_arrow_head, true, 0);
    lv_obj_set_style_line_color(m_arrow_head, UI_COLOR_ACCENT, 0);
    lv_line_set_points(m_arrow_head, m_head_pts, 3);
    lv_obj_remove_flag(m_arrow_head, LV_OBJ_FLAG_CLICKABLE);

    m_arrow_flag = lv_line_create(m_maneuver_box);
    lv_obj_set_style_line_width(m_arrow_flag, 6, 0);
    lv_obj_set_style_line_rounded(m_arrow_flag, true, 0);
    lv_obj_set_style_line_color(m_arrow_flag, UI_COLOR_OK, 0);
    lv_line_set_points(m_arrow_flag, m_flag_pts, 6);
    lv_obj_remove_flag(m_arrow_flag, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(m_arrow_flag, LV_OBJ_FLAG_HIDDEN);

    m_roundabout_arc = lv_arc_create(m_maneuver_box);
    lv_obj_set_size(m_roundabout_arc, 88, 88);
    lv_obj_set_pos(m_roundabout_arc, 31, 28);
    lv_arc_set_bg_angles(m_roundabout_arc, 35, 325);
    lv_arc_set_rotation(m_roundabout_arc, 0);
    lv_obj_set_style_arc_width(m_roundabout_arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_color(m_roundabout_arc, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(m_roundabout_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(m_roundabout_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_remove_flag(m_roundabout_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(m_roundabout_arc, LV_OBJ_FLAG_HIDDEN);

    // One-line caption in sentence form ("TURN LEFT"), not two cramped lines:
    // Montserrat 28 needs ~66 px for two lines and the old 44 px box clipped it.
    m_maneuver_caption = lv_label_create(m_maneuver_box);
    lv_label_set_text(m_maneuver_caption, "STRAIGHT");
    lv_obj_set_style_text_color(m_maneuver_caption, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(m_maneuver_caption, UI_FONT_XL, 0);
    lv_obj_set_size(m_maneuver_caption, 138, 30);
    lv_obj_set_pos(m_maneuver_caption, 6, 184);
    lv_obj_set_style_text_align(m_maneuver_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(m_maneuver_caption, LV_LABEL_LONG_MODE_DOTS);

    m_roundabout_badge = lv_obj_create(m_maneuver_box);
    lv_obj_set_size(m_roundabout_badge, 64, 22);
    lv_obj_set_pos(m_roundabout_badge, 43, 132);
    lv_obj_set_style_bg_color(m_roundabout_badge, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(m_roundabout_badge, 0, 0);
    lv_obj_set_style_radius(m_roundabout_badge, 5, 0);
    lv_obj_set_style_pad_all(m_roundabout_badge, 0, 0);
    lv_obj_remove_flag(m_roundabout_badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(m_roundabout_badge, LV_OBJ_FLAG_HIDDEN);

    m_roundabout_label = lv_label_create(m_roundabout_badge);
    lv_label_set_text(m_roundabout_label, "Exit 1");
    lv_obj_set_style_text_color(m_roundabout_label, UI_COLOR_BG, 0);
    lv_obj_set_style_text_font(m_roundabout_label, UI_FONT_SM, 0);
    lv_obj_center(m_roundabout_label);

    // --- Compass rose (standby only) -------------------------------------
    m_compass_ring = lv_obj_create(m_maneuver_box);
    lv_obj_set_size(m_compass_ring, kCompassR * 2, kCompassR * 2);
    lv_obj_set_pos(m_compass_ring, 15, 8);
    lv_obj_set_style_bg_opa(m_compass_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(m_compass_ring, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_border_width(m_compass_ring, 2, 0);
    lv_obj_set_style_radius(m_compass_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(m_compass_ring, 0, 0);
    lv_obj_remove_flag(m_compass_ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(m_compass_ring, LV_OBJ_FLAG_CLICKABLE);

    static const char *kCardinalText[4] = {"N", "E", "S", "W"};
    static const lv_coord_t kCardinalX[4] = {63, 108, 63, 18};
    static const lv_coord_t kCardinalY[4] = {12, 61, 110, 61};
    for (int i = 0; i < 4; i++) {
        m_cardinal_labels[i] = lv_label_create(m_maneuver_box);
        lv_label_set_text(m_cardinal_labels[i], kCardinalText[i]);
        lv_obj_set_style_text_color(m_cardinal_labels[i], UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(m_cardinal_labels[i], UI_FONT_SM, 0);
        lv_obj_set_size(m_cardinal_labels[i], 24, 16);
        lv_obj_set_pos(m_cardinal_labels[i], kCardinalX[i], kCardinalY[i]);
        lv_obj_set_style_text_align(m_cardinal_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    }

    // Diagonal ticks give the ring a rose cadence without colliding with the
    // four cardinal letters. Coordinates are the NE, SE, SW and NW marks.
    static const int16_t kTickStart[4][2] = {{109, 34}, {109, 102}, {41, 102}, {41, 34}};
    static const int16_t kTickEnd[4][2] = {{115, 28}, {115, 108}, {35, 108}, {35, 28}};
    for (int i = 0; i < 4; i++) {
        m_tick_pts[i][0] = {kTickStart[i][0], kTickStart[i][1]};
        m_tick_pts[i][1] = {kTickEnd[i][0], kTickEnd[i][1]};
        m_tick_lines[i] = lv_line_create(m_maneuver_box);
        lv_obj_set_style_line_width(m_tick_lines[i], 2, 0);
        lv_obj_set_style_line_rounded(m_tick_lines[i], true, 0);
        lv_obj_set_style_line_color(m_tick_lines[i], UI_COLOR_TEXT_DIM, 0);
        lv_line_set_points(m_tick_lines[i], m_tick_pts[i], 2);
        lv_obj_remove_flag(m_tick_lines[i], LV_OBJ_FLAG_CLICKABLE);
    }

    m_compass_needle = lv_line_create(m_maneuver_box);
    lv_obj_set_style_line_width(m_compass_needle, 4, 0);
    lv_obj_set_style_line_rounded(m_compass_needle, true, 0);
    lv_obj_set_style_line_color(m_compass_needle, UI_COLOR_ACCENT, 0);
    lv_line_set_points(m_compass_needle, m_needle_pts, 2);
    lv_obj_remove_flag(m_compass_needle, LV_OBJ_FLAG_CLICKABLE);

    m_compass_tail = lv_line_create(m_maneuver_box);
    lv_obj_set_style_line_width(m_compass_tail, 3, 0);
    lv_obj_set_style_line_rounded(m_compass_tail, true, 0);
    lv_obj_set_style_line_color(m_compass_tail, UI_COLOR_TEXT_DIM, 0);
    lv_line_set_points(m_compass_tail, m_tail_pts, 2);
    lv_obj_remove_flag(m_compass_tail, LV_OBJ_FLAG_CLICKABLE);

    m_compass_hub = lv_obj_create(m_maneuver_box);
    lv_obj_set_size(m_compass_hub, 10, 10);
    lv_obj_set_pos(m_compass_hub, kCompassCx - 5, kCompassCy - 5);
    lv_obj_set_style_bg_color(m_compass_hub, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(m_compass_hub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(m_compass_hub, 0, 0);
    lv_obj_set_style_radius(m_compass_hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(m_compass_hub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(m_compass_hub, LV_OBJ_FLAG_CLICKABLE);

    m_compass_heading_label = lv_label_create(m_maneuver_box);
    lv_label_set_text(m_compass_heading_label, "--");
    lv_obj_set_style_text_color(m_compass_heading_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_compass_heading_label, UI_FONT_TITLE, 0);
    lv_obj_set_size(m_compass_heading_label, 138, 36);
    lv_obj_set_pos(m_compass_heading_label, 6, 136);
    lv_obj_set_style_text_align(m_compass_heading_label, LV_TEXT_ALIGN_CENTER, 0);

    m_compass_sub_label = lv_label_create(m_maneuver_box);
    lv_label_set_text(m_compass_sub_label, "NO HEADING");
    lv_obj_set_style_text_color(m_compass_sub_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(m_compass_sub_label, UI_FONT_SM, 0);
    lv_obj_set_size(m_compass_sub_label, 138, 18);
    lv_obj_set_pos(m_compass_sub_label, 6, 176);
    lv_obj_set_style_text_align(m_compass_sub_label, LV_TEXT_ALIGN_CENTER, 0);

    // -----------------------------------------------------------------------
    // 2. Instruction card (right). Active route => turn cue. Standby =>
    //    GPS speed cluster. The two never share a screen.
    // -----------------------------------------------------------------------
    m_instruction_card = lv_obj_create(m_screen);
    lv_obj_set_size(m_instruction_card, 310, 232);
    lv_obj_set_pos(m_instruction_card, 162, 8);
    lv_obj_set_style_bg_color(m_instruction_card, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(m_instruction_card, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_border_width(m_instruction_card, 1, 0);
    lv_obj_set_style_radius(m_instruction_card, 12, 0);
    lv_obj_set_style_pad_all(m_instruction_card, 0, 0);
    lv_obj_remove_flag(m_instruction_card, LV_OBJ_FLAG_SCROLLABLE);

    m_distance_label = lv_label_create(m_instruction_card);
    lv_label_set_text(m_distance_label, "--");
    lv_obj_set_style_text_color(m_distance_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_distance_label, UI_FONT_DIGITS_48, 0);
    lv_obj_set_pos(m_distance_label, 10, 8);
    lv_obj_set_size(m_distance_label, 290, 52);

    m_street_label = lv_label_create(m_instruction_card);
    lv_label_set_text(m_street_label, "");
    lv_obj_set_style_text_color(m_street_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_street_label, UI_FONT_TITLE, 0);
    lv_obj_set_pos(m_street_label, 10, 64);
    lv_obj_set_size(m_street_label, 290, 34);
    lv_label_set_long_mode(m_street_label, LV_LABEL_LONG_MODE_DOTS);

    m_next_street_label = lv_label_create(m_instruction_card);
    lv_label_set_text(m_next_street_label, "");
    lv_obj_set_style_text_color(m_next_street_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(m_next_street_label, UI_FONT_MD, 0);
    lv_obj_set_pos(m_next_street_label, 10, 102);
    lv_obj_set_size(m_next_street_label, 290, 24);

    // Standby speed cluster.
    m_standby_header = lv_label_create(m_instruction_card);
    lv_label_set_text(m_standby_header, "GPS SPEED");
    lv_obj_set_style_text_color(m_standby_header, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(m_standby_header, UI_FONT_SM, 0);
    lv_obj_set_pos(m_standby_header, 14, 12);

    m_standby_link_dot = lv_obj_create(m_instruction_card);
    lv_obj_set_size(m_standby_link_dot, 8, 8);
    lv_obj_set_pos(m_standby_link_dot, 272, 15);
    lv_obj_set_style_bg_color(m_standby_link_dot, UI_COLOR_OK, 0);
    lv_obj_set_style_bg_opa(m_standby_link_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(m_standby_link_dot, 0, 0);
    lv_obj_set_style_radius(m_standby_link_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(m_standby_link_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(m_standby_link_dot, LV_OBJ_FLAG_CLICKABLE);

    m_standby_speed_digits = lv_label_create(m_instruction_card);
    lv_label_set_text(m_standby_speed_digits, "--");
    lv_obj_set_style_text_color(m_standby_speed_digits, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_standby_speed_digits, UI_FONT_DIGITS_48, 0);
    lv_obj_set_pos(m_standby_speed_digits, 14, 28);
    lv_obj_set_size(m_standby_speed_digits, 180, 58);

    m_standby_speed_unit = lv_label_create(m_instruction_card);
    lv_label_set_text(m_standby_speed_unit, "km/h");
    lv_obj_set_style_text_color(m_standby_speed_unit, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(m_standby_speed_unit, UI_FONT_MD, 0);
    lv_obj_set_pos(m_standby_speed_unit, 116, 76);
    lv_obj_set_size(m_standby_speed_unit, 80, 20);

    m_speed_bar = lv_bar_create(m_instruction_card);
    lv_obj_set_size(m_speed_bar, 200, 8);
    lv_obj_set_pos(m_speed_bar, 14, 104);
    lv_bar_set_range(m_speed_bar, 0, 200);
    lv_bar_set_value(m_speed_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(m_speed_bar, UI_COLOR_SURFACE2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m_speed_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(m_speed_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(m_speed_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(m_speed_bar, 4, LV_PART_INDICATOR);
    lv_obj_remove_flag(m_speed_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(m_speed_bar, LV_OBJ_FLAG_SCROLLABLE);

    m_speed_limit_badge = lv_obj_create(m_instruction_card);
    lv_obj_set_size(m_speed_limit_badge, 64, 64);
    lv_obj_set_pos(m_speed_limit_badge, 228, 28);
    lv_obj_set_style_bg_color(m_speed_limit_badge, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(m_speed_limit_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(m_speed_limit_badge, UI_COLOR_DANGER, 0);
    lv_obj_set_style_border_width(m_speed_limit_badge, 5, 0);
    lv_obj_set_style_radius(m_speed_limit_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(m_speed_limit_badge, 0, 0);
    lv_obj_remove_flag(m_speed_limit_badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(m_speed_limit_badge, LV_OBJ_FLAG_HIDDEN);

    m_speed_limit_val = lv_label_create(m_speed_limit_badge);
    lv_label_set_text(m_speed_limit_val, "--");
    lv_obj_set_style_text_font(m_speed_limit_val, UI_FONT_XL, 0);
    lv_obj_set_style_text_color(m_speed_limit_val, lv_color_hex(0x000000), 0);
    lv_obj_center(m_speed_limit_val);

    m_standby_street_label = lv_label_create(m_instruction_card);
    lv_label_set_text(m_standby_street_label, "");
    lv_obj_set_style_text_color(m_standby_street_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_standby_street_label, UI_FONT_TITLE, 0);
    lv_obj_set_pos(m_standby_street_label, 14, 120);
    lv_obj_set_size(m_standby_street_label, 282, 36);
    lv_label_set_long_mode(m_standby_street_label, LV_LABEL_LONG_MODE_DOTS);

    // Bottom strip shared by both presentations.
    m_info_strip = lv_obj_create(m_instruction_card);
    lv_obj_set_size(m_info_strip, 310, 68);
    lv_obj_set_pos(m_info_strip, 0, 164);
    lv_obj_set_style_bg_color(m_info_strip, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_bg_opa(m_info_strip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(m_info_strip, 0, 0);
    lv_obj_set_style_radius(m_info_strip, 0, 0);
    lv_obj_set_style_pad_all(m_info_strip, 0, 0);
    lv_obj_remove_flag(m_info_strip, LV_OBJ_FLAG_SCROLLABLE);

    m_remaining_dist_label = lv_label_create(m_info_strip);
    lv_label_set_text(m_remaining_dist_label, "");
    lv_obj_set_style_text_color(m_remaining_dist_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_remaining_dist_label, UI_FONT_MD, 0);
    lv_obj_set_pos(m_remaining_dist_label, 14, 10);

    m_eta_label = lv_label_create(m_info_strip);
    lv_label_set_text(m_eta_label, "");
    lv_obj_set_style_text_color(m_eta_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(m_eta_label, UI_FONT_MD, 0);
    lv_obj_set_pos(m_eta_label, 14, 38);

    m_end_nav_btn = lv_button_create(m_info_strip);
    lv_obj_set_size(m_end_nav_btn, 76, 48);
    lv_obj_align(m_end_nav_btn, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_style_bg_color(m_end_nav_btn, UI_COLOR_DANGER, 0);
    lv_obj_set_style_bg_color(m_end_nav_btn, UI_COLOR_WARN, LV_STATE_PRESSED);
    lv_obj_set_style_radius(m_end_nav_btn, 8, 0);
    lv_obj_set_style_pad_all(m_end_nav_btn, 0, 0);
    lv_obj_set_ext_click_area(m_end_nav_btn, 8);
    lv_obj_remove_flag(m_end_nav_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(m_end_nav_btn, onEndNavClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(m_end_nav_btn, onEndNavClicked, LV_EVENT_SHORT_CLICKED, this);

    lv_obj_t *end_lbl = lv_label_create(m_end_nav_btn);
    lv_label_set_text(end_lbl, "End");
    lv_obj_set_style_text_color(end_lbl, UI_COLOR_BG, 0);
    lv_obj_set_style_text_font(end_lbl, UI_FONT_MD, 0);
    lv_obj_center(end_lbl);
    lv_obj_remove_flag(end_lbl, LV_OBJ_FLAG_CLICKABLE);

    m_standby_status_label = lv_label_create(m_info_strip);
    lv_label_set_text(m_standby_status_label, "GPS: searching");
    lv_obj_set_style_text_color(m_standby_status_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_standby_status_label, UI_FONT_MD, 0);
    lv_obj_set_pos(m_standby_status_label, 14, 10);

    m_standby_accuracy_label = lv_label_create(m_info_strip);
    lv_label_set_text(m_standby_accuracy_label, "Waiting for fix");
    lv_obj_set_style_text_color(m_standby_accuracy_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(m_standby_accuracy_label, UI_FONT_SM, 0);
    lv_obj_set_pos(m_standby_accuracy_label, 14, 38);

    m_standby_maps_btn = lv_button_create(m_info_strip);
    lv_obj_set_size(m_standby_maps_btn, 84, 48);
    lv_obj_align(m_standby_maps_btn, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_style_bg_color(m_standby_maps_btn, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_color(m_standby_maps_btn, UI_COLOR_SURFACE2, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(m_standby_maps_btn, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(m_standby_maps_btn, 1, 0);
    lv_obj_set_style_radius(m_standby_maps_btn, 8, 0);
    lv_obj_set_ext_click_area(m_standby_maps_btn, 8);
    lv_obj_remove_flag(m_standby_maps_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(m_standby_maps_btn, onLaunchMapsClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(m_standby_maps_btn, onLaunchMapsClicked, LV_EVENT_SHORT_CLICKED, this);

    lv_obj_t *maps_lbl = lv_label_create(m_standby_maps_btn);
    lv_label_set_text(maps_lbl, LV_SYMBOL_GPS " Maps");
    lv_obj_set_style_text_color(maps_lbl, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(maps_lbl, UI_FONT_SM, 0);
    lv_obj_center(maps_lbl);
    lv_obj_remove_flag(maps_lbl, LV_OBJ_FLAG_CLICKABLE);

    // -----------------------------------------------------------------------
    // 3. Offline card. Covers the workspace instead of leaving two dead
    //    gauges on screen. This is the first thing a new owner sees.
    // -----------------------------------------------------------------------
    m_offline_card = lv_obj_create(m_screen);
    lv_obj_set_size(m_offline_card, 440, 212);
    lv_obj_set_pos(m_offline_card, 20, 18);
    lv_obj_set_style_bg_color(m_offline_card, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(m_offline_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(m_offline_card, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_border_width(m_offline_card, 1, 0);
    lv_obj_set_style_radius(m_offline_card, 14, 0);
    lv_obj_set_style_pad_all(m_offline_card, 0, 0);
    lv_obj_remove_flag(m_offline_card, LV_OBJ_FLAG_SCROLLABLE);

    m_offline_icon = lv_label_create(m_offline_card);
    lv_label_set_text(m_offline_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(m_offline_icon, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(m_offline_icon, UI_FONT_TITLE, 0);
    lv_obj_align(m_offline_icon, LV_ALIGN_TOP_MID, 0, 12);

    m_offline_title = lv_label_create(m_offline_card);
    lv_label_set_text(m_offline_title, "PHONE NOT CONNECTED");
    lv_obj_set_style_text_color(m_offline_title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_offline_title, UI_FONT_TITLE, 0);
    lv_obj_set_size(m_offline_title, 400, 34);
    lv_obj_set_pos(m_offline_title, 20, 52);
    lv_obj_set_style_text_align(m_offline_title, LV_TEXT_ALIGN_CENTER, 0);

    m_offline_body = lv_label_create(m_offline_card);
    lv_label_set_text(m_offline_body,
                      "Navigation and media mirror over Bluetooth.\n"
                      "Open DashCue on your phone to connect.");
    lv_obj_set_style_text_color(m_offline_body, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(m_offline_body, UI_FONT_MD, 0);
    lv_obj_set_size(m_offline_body, 400, 44);
    lv_obj_set_pos(m_offline_body, 20, 92);
    lv_obj_set_style_text_align(m_offline_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(m_offline_body, LV_LABEL_LONG_MODE_WRAP);

    m_offline_hint = lv_label_create(m_offline_card);
    lv_label_set_text(m_offline_hint, "Waiting for a paired phone");
    lv_obj_set_style_text_color(m_offline_hint, UI_COLOR_WARN, 0);
    lv_obj_set_style_text_font(m_offline_hint, UI_FONT_SM, 0);
    lv_obj_set_size(m_offline_hint, 400, 18);
    lv_obj_set_pos(m_offline_hint, 20, 140);
    lv_obj_set_style_text_align(m_offline_hint, LV_TEXT_ALIGN_CENTER, 0);

    m_offline_pair_btn = lv_button_create(m_offline_card);
    lv_obj_set_size(m_offline_pair_btn, 220, 48);
    lv_obj_align(m_offline_pair_btn, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(m_offline_pair_btn, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_color(m_offline_pair_btn, UI_COLOR_WARN, LV_STATE_PRESSED);
    lv_obj_set_style_radius(m_offline_pair_btn, 8, 0);
    lv_obj_set_style_pad_all(m_offline_pair_btn, 0, 0);
    lv_obj_set_ext_click_area(m_offline_pair_btn, 10);
    lv_obj_remove_flag(m_offline_pair_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(m_offline_pair_btn, onPairClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(m_offline_pair_btn, onPairClicked, LV_EVENT_SHORT_CLICKED, this);

    lv_obj_t *pair_lbl = lv_label_create(m_offline_pair_btn);
    lv_label_set_text(pair_lbl, "Start pairing");
    lv_obj_set_style_text_color(pair_lbl, UI_COLOR_BG, 0);
    lv_obj_set_style_text_font(pair_lbl, UI_FONT_MD, 0);
    lv_obj_center(pair_lbl);
    lv_obj_remove_flag(pair_lbl, LV_OBJ_FLAG_CLICKABLE);

    // Stale banner (active navigation only).
    m_stale_overlay = lv_obj_create(m_screen);
    lv_obj_set_size(m_stale_overlay, UI_SCREEN_W, 24);
    lv_obj_set_pos(m_stale_overlay, 0, 0);
    lv_obj_set_style_bg_color(m_stale_overlay, UI_COLOR_WARN, 0);
    lv_obj_set_style_bg_opa(m_stale_overlay, LV_OPA_80, 0);
    lv_obj_set_style_border_width(m_stale_overlay, 0, 0);
    lv_obj_set_style_radius(m_stale_overlay, 0, 0);
    lv_obj_set_style_pad_all(m_stale_overlay, 0, 0);
    lv_obj_remove_flag(m_stale_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(m_stale_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *stale_text = lv_label_create(m_stale_overlay);
    lv_label_set_text(stale_text, LV_SYMBOL_WARNING " NAV DATA STALE - CHECK PHONE");
    lv_obj_set_style_text_color(stale_text, UI_COLOR_BG, 0);
    lv_obj_set_style_text_font(stale_text, UI_FONT_SM, 0);
    lv_obj_center(stale_text);

    // Confirm End modal (two driver-sized targets, one destructive action).
    m_confirm_modal = lv_obj_create(parent);
    lv_obj_set_size(m_confirm_modal, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(m_confirm_modal, 0, 0);
    lv_obj_set_style_bg_color(m_confirm_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(m_confirm_modal, LV_OPA_80, 0);
    lv_obj_set_style_border_width(m_confirm_modal, 0, 0);
    lv_obj_set_style_radius(m_confirm_modal, 0, 0);
    lv_obj_set_style_pad_all(m_confirm_modal, 0, 0);
    lv_obj_remove_flag(m_confirm_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dialog_box = lv_obj_create(m_confirm_modal);
    lv_obj_set_size(dialog_box, 380, 180);
    lv_obj_center(dialog_box);
    lv_obj_set_style_bg_color(dialog_box, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(dialog_box, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_border_width(dialog_box, 2, 0);
    lv_obj_set_style_radius(dialog_box, 14, 0);
    lv_obj_remove_flag(dialog_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dialog_title = lv_label_create(dialog_box);
    lv_label_set_text(dialog_title, "End Current Route?");
    lv_obj_set_style_text_font(dialog_title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(dialog_title, UI_COLOR_TEXT, 0);
    lv_obj_align(dialog_title, LV_ALIGN_TOP_MID, 0, 14);

    m_btn_modal_cancel = lv_button_create(dialog_box);
    lv_obj_set_size(m_btn_modal_cancel, 160, 56);
    lv_obj_align(m_btn_modal_cancel, LV_ALIGN_BOTTOM_LEFT, 16, -14);
    lv_obj_set_style_bg_color(m_btn_modal_cancel, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_radius(m_btn_modal_cancel, 10, 0);
    lv_obj_set_ext_click_area(m_btn_modal_cancel, 8);
    lv_obj_remove_flag(m_btn_modal_cancel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(m_btn_modal_cancel, onModalCancelClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(m_btn_modal_cancel, onModalCancelClicked, LV_EVENT_SHORT_CLICKED, this);

    lv_obj_t *cancel_text = lv_label_create(m_btn_modal_cancel);
    lv_label_set_text(cancel_text, "Cancel");
    lv_obj_set_style_text_font(cancel_text, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(cancel_text, UI_COLOR_TEXT, 0);
    lv_obj_center(cancel_text);
    lv_obj_remove_flag(cancel_text, LV_OBJ_FLAG_CLICKABLE);

    m_btn_modal_confirm = lv_button_create(dialog_box);
    lv_obj_set_size(m_btn_modal_confirm, 160, 56);
    lv_obj_align(m_btn_modal_confirm, LV_ALIGN_BOTTOM_RIGHT, -16, -14);
    lv_obj_set_style_bg_color(m_btn_modal_confirm, UI_COLOR_DANGER, 0);
    lv_obj_set_style_radius(m_btn_modal_confirm, 10, 0);
    lv_obj_set_ext_click_area(m_btn_modal_confirm, 8);
    lv_obj_remove_flag(m_btn_modal_confirm, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(m_btn_modal_confirm, onModalConfirmClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(m_btn_modal_confirm, onModalConfirmClicked, LV_EVENT_SHORT_CLICKED, this);

    lv_obj_t *confirm_text = lv_label_create(m_btn_modal_confirm);
    lv_label_set_text(confirm_text, "End Route");
    lv_obj_set_style_text_font(confirm_text, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(confirm_text, UI_COLOR_TEXT, 0);
    lv_obj_center(confirm_text);
    lv_obj_remove_flag(confirm_text, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_flag(m_confirm_modal, LV_OBJ_FLAG_HIDDEN);

    // Boot into the honest state: nothing is connected yet.
    setMode(MODE_OFFLINE);
}

void UiScreenNav::destroy() {
    // The confirmation overlay is a sibling of the workspace screen.
    if (m_confirm_modal) lv_obj_delete(m_confirm_modal);
    if (m_screen) lv_obj_delete(m_screen);
    *this = UiScreenNav();
}

void UiScreenNav::show() {
    if (m_screen) lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenNav::hide() {
    if (m_screen) lv_obj_add_flag(m_screen, LV_OBJ_FLAG_HIDDEN);
    if (m_confirm_modal) lv_obj_add_flag(m_confirm_modal, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenNav::setImperial(bool imperial) {
    if (m_imperial == imperial) return;
    m_imperial = imperial;
}

void UiScreenNav::setLinkState(bool connected, bool bonded) {
    m_connected = connected;
    m_bonded = bonded;
    if (!m_screen) return;

    if (!connected) {
        m_route_active = false;
        setMode(MODE_OFFLINE);
        if (m_offline_title) {
            lv_label_set_text(m_offline_title,
                              bonded ? "PHONE OFFLINE" : "PHONE NOT CONNECTED");
        }
        if (m_offline_body) {
            lv_label_set_text(m_offline_body,
                              bonded
                                  ? "This HUD is paired, but the phone link is down.\n"
                                    "Open DashCue on your phone to reconnect."
                                  : "Navigation and media mirror over Bluetooth.\n"
                                    "Open DashCue on your phone to connect.");
        }
        if (m_offline_hint) {
            lv_label_set_text(m_offline_hint,
                              bonded ? "Paired phone is out of range"
                                     : "Waiting for a paired phone");
            lv_obj_set_style_text_color(m_offline_hint, UI_COLOR_WARN, 0);
        }
    } else {
        setMode(m_route_active ? MODE_ACTIVE_NAV : MODE_STANDBY);
    }
}

void UiScreenNav::updateCompass(float heading_deg) {
    const bool valid = isfinite(heading_deg) && heading_deg >= 0.0f;
    m_has_heading = valid;

    if (!valid) {
        if (m_compass_needle) lv_obj_add_flag(m_compass_needle, LV_OBJ_FLAG_HIDDEN);
        if (m_compass_tail) lv_obj_add_flag(m_compass_tail, LV_OBJ_FLAG_HIDDEN);
        if (m_compass_hub) lv_obj_add_flag(m_compass_hub, LV_OBJ_FLAG_HIDDEN);
        if (m_compass_heading_label) lv_label_set_text(m_compass_heading_label, "--");
        if (m_compass_sub_label) {
            lv_label_set_text(m_compass_sub_label, "NO HEADING");
            lv_obj_set_style_text_color(m_compass_sub_label, UI_COLOR_TEXT_DIM, 0);
        }
        return;
    }

    while (heading_deg < 0.0f) heading_deg += 360.0f;
    while (heading_deg >= 360.0f) heading_deg -= 360.0f;
    const char *cardinal = cardinal_from_bearing(heading_deg);

    if (m_compass_needle) lv_obj_remove_flag(m_compass_needle, LV_OBJ_FLAG_HIDDEN);
    if (m_compass_tail) lv_obj_remove_flag(m_compass_tail, LV_OBJ_FLAG_HIDDEN);
    if (m_compass_hub) lv_obj_remove_flag(m_compass_hub, LV_OBJ_FLAG_HIDDEN);

    if (m_compass_heading_label) {
        char buf[16];
        // Plain ASCII: the shipped Montserrat builds carry no degree glyph, so
        // "128°" rendered as a missing-glyph box before the cardinal label.
        snprintf(buf, sizeof(buf), "%d", (int)(heading_deg + 0.5f) % 360);
        lv_label_set_text(m_compass_heading_label, buf);
    }
    if (m_compass_sub_label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s  COMPASS", cardinal);
        lv_label_set_text(m_compass_sub_label, buf);
        lv_obj_set_style_text_color(m_compass_sub_label, UI_COLOR_TEXT_DIM, 0);
    }

    // Screen bearing: 0 deg = up. The wire convention is clockwise from north,
    // so subtract 90 deg when converting to LVGL's screen axes.
    const float rad = (heading_deg - 90.0f) * 3.14159265f / 180.0f;
    const lv_coord_t tip_x = (lv_coord_t)(kCompassCx + 40 * cosf(rad));
    const lv_coord_t tip_y = (lv_coord_t)(kCompassCy + 40 * sinf(rad));
    const lv_coord_t tail_x = (lv_coord_t)(kCompassCx - 30 * cosf(rad));
    const lv_coord_t tail_y = (lv_coord_t)(kCompassCy - 30 * sinf(rad));

    if (m_compass_needle) {
        m_needle_pts[0] = {kCompassCx, kCompassCy};
        m_needle_pts[1] = {tip_x, tip_y};
        lv_line_set_points(m_compass_needle, m_needle_pts, 2);
    }
    if (m_compass_tail) {
        m_tail_pts[0] = {kCompassCx, kCompassCy};
        m_tail_pts[1] = {tail_x, tail_y};
        lv_line_set_points(m_compass_tail, m_tail_pts, 2);
    }
}

void UiScreenNav::updateSpeedometer(int32_t speed_kmh_x10, uint32_t speed_limit_kmh) {
    if (!m_standby_speed_digits || !m_standby_speed_unit) return;

    const bool has_speed = speed_kmh_x10 >= 0;
    int speed = 0;
    if (has_speed) {
        speed = (int)((speed_kmh_x10 / 10.0f) + 0.5f);
        if (m_imperial) speed = (int)(speed / 1.609344f + 0.5f);
    }

    char s_buf[16];
    if (has_speed) {
        snprintf(s_buf, sizeof(s_buf), "%d", speed);
    } else {
        snprintf(s_buf, sizeof(s_buf), "--");
    }
    lv_label_set_text(m_standby_speed_digits, s_buf);
    lv_label_set_text(m_standby_speed_unit, m_imperial ? "mph" : "km/h");

    const bool overspeed = has_speed && speed_limit_kmh > 0 &&
                           speed_kmh_x10 > (int32_t)(speed_limit_kmh * 10);
    lv_obj_set_style_text_color(m_standby_speed_digits,
                                overspeed ? UI_COLOR_DANGER : UI_COLOR_TEXT, 0);

    if (m_speed_bar) {
        int bar_value = has_speed ? speed : 0;
        if (bar_value < 0) bar_value = 0;
        if (bar_value > 200) bar_value = 200;
        lv_bar_set_value(m_speed_bar, bar_value, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(m_speed_bar,
                                  overspeed ? UI_COLOR_DANGER : UI_COLOR_ACCENT,
                                  LV_PART_INDICATOR);
    }

    if (!m_speed_limit_badge || !m_speed_limit_val) return;
    if (speed_limit_kmh == 0 || speed_limit_kmh > 200) {
        // No source published a limit. Hide the disc rather than show a blank
        // white circle, which drivers read as "no limit".
        lv_obj_add_flag(m_speed_limit_badge, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const uint32_t display_limit = m_imperial
                                       ? (uint32_t)(speed_limit_kmh / 1.609344f + 0.5f)
                                       : speed_limit_kmh;
    char lim_buf[8];
    snprintf(lim_buf, sizeof(lim_buf), "%u", (unsigned)display_limit);
    lv_label_set_text(m_speed_limit_val, lim_buf);
    lv_obj_remove_flag(m_speed_limit_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_border_width(m_speed_limit_badge, overspeed ? 8 : 5, 0);
    lv_obj_set_style_bg_color(m_speed_limit_badge,
                              lv_color_hex(overspeed ? 0xFFECEC : 0xFFFFFF), 0);
}

void UiScreenNav::setMode(Mode mode) {
    m_mode = mode;
    if (!m_screen) return;

    const bool offline = (mode == MODE_OFFLINE);
    const bool standby = (mode == MODE_STANDBY);
    const bool active = (mode == MODE_ACTIVE_NAV || mode == MODE_CALCULATING);

    if (offline) {
        lv_obj_add_flag(m_maneuver_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_instruction_card, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(m_maneuver_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(m_instruction_card, LV_OBJ_FLAG_HIDDEN);
    }
    if (m_offline_card) {
        if (offline) lv_obj_remove_flag(m_offline_card, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(m_offline_card, LV_OBJ_FLAG_HIDDEN);
    }
    if (m_stale_overlay) lv_obj_add_flag(m_stale_overlay, LV_OBJ_FLAG_HIDDEN);

    // Compass group.
    const bool compass_visible = standby;
    lv_obj_t *compass_objs[] = {m_compass_ring, m_compass_heading_label, m_compass_sub_label};
    for (lv_obj_t *obj : compass_objs) {
        if (!obj) continue;
        if (compass_visible) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 4; i++) {
        if (m_cardinal_labels[i]) {
            if (compass_visible) lv_obj_remove_flag(m_cardinal_labels[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(m_cardinal_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (m_tick_lines[i]) {
            if (compass_visible) lv_obj_remove_flag(m_tick_lines[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(m_tick_lines[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    const bool needle_visible = compass_visible && m_has_heading;
    lv_obj_t *needle_objs[] = {m_compass_needle, m_compass_tail, m_compass_hub};
    for (lv_obj_t *obj : needle_objs) {
        if (!obj) continue;
        if (needle_visible) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

    // Speed cluster group.
    lv_obj_t *speed_objs[] = {m_standby_header, m_standby_link_dot, m_standby_street_label,
                              m_standby_speed_digits, m_standby_speed_unit, m_speed_bar,
                              m_standby_status_label, m_standby_accuracy_label};
    for (lv_obj_t *obj : speed_objs) {
        if (!obj) continue;
        if (standby) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    if (m_standby_maps_btn) {
        if (standby) lv_obj_remove_flag(m_standby_maps_btn, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(m_standby_maps_btn, LV_OBJ_FLAG_HIDDEN);
    }

    // Active-route group.
    lv_obj_t *active_objs[] = {m_distance_label, m_street_label, m_next_street_label,
                               m_remaining_dist_label, m_eta_label, m_end_nav_btn};
    for (lv_obj_t *obj : active_objs) {
        if (!obj) continue;
        if (active) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    if (m_maneuver_caption) {
        if (active) lv_obj_remove_flag(m_maneuver_caption, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(m_maneuver_caption, LV_OBJ_FLAG_HIDDEN);
    }
    if (m_roundabout_badge) lv_obj_add_flag(m_roundabout_badge, LV_OBJ_FLAG_HIDDEN);
    if (m_roundabout_arc) lv_obj_add_flag(m_roundabout_arc, LV_OBJ_FLAG_HIDDEN);
    if (m_arrow_flag) lv_obj_add_flag(m_arrow_flag, LV_OBJ_FLAG_HIDDEN);
    if (m_arrow_shaft) lv_obj_add_flag(m_arrow_shaft, LV_OBJ_FLAG_HIDDEN);
    if (m_arrow_head) lv_obj_add_flag(m_arrow_head, LV_OBJ_FLAG_HIDDEN);

    // Speed-limit disc belongs to standby only; updateSpeedometer re-shows it.
    if (m_speed_limit_badge && !standby) {
        lv_obj_add_flag(m_speed_limit_badge, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiScreenNav::layoutStandby() {
    setMode(MODE_STANDBY);
}

void UiScreenNav::layoutActive() {
    setMode(MODE_ACTIVE_NAV);
}

void UiScreenNav::applyStale(bool stale) {
    if (!m_stale_overlay) return;
    if (stale && m_mode != MODE_OFFLINE) {
        lv_obj_remove_flag(m_stale_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(m_stale_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiScreenNav::applyManeuver(dashcue_v1_Maneuver maneuver) {
    bool show_flag = false;
    bool show_arc = false;
    const char *caption = "STRAIGHT";
    lv_color_t color = UI_COLOR_ACCENT;

    auto set_left = [this]() {
        m_shaft_pt_count = 3;
        m_shaft_pts[0] = {95, 135}; m_shaft_pts[1] = {95, 75}; m_shaft_pts[2] = {38, 75};
        m_head_pts[0] = {62, 48}; m_head_pts[1] = {34, 75}; m_head_pts[2] = {62, 102};
    };
    auto set_right = [this]() {
        m_shaft_pt_count = 3;
        m_shaft_pts[0] = {55, 135}; m_shaft_pts[1] = {55, 75}; m_shaft_pts[2] = {112, 75};
        m_head_pts[0] = {88, 48}; m_head_pts[1] = {116, 75}; m_head_pts[2] = {88, 102};
    };
    auto set_slight_left = [this]() {
        m_shaft_pt_count = 3;
        m_shaft_pts[0] = {82, 135}; m_shaft_pts[1] = {82, 88}; m_shaft_pts[2] = {48, 42};
        m_head_pts[0] = {36, 64}; m_head_pts[1] = {44, 38}; m_head_pts[2] = {70, 48};
    };
    auto set_slight_right = [this]() {
        m_shaft_pt_count = 3;
        m_shaft_pts[0] = {68, 135}; m_shaft_pts[1] = {68, 88}; m_shaft_pts[2] = {102, 42};
        m_head_pts[0] = {80, 48}; m_head_pts[1] = {106, 38}; m_head_pts[2] = {114, 64};
    };
    auto set_sharp_left = [this]() {
        m_shaft_pt_count = 3;
        m_shaft_pts[0] = {96, 135}; m_shaft_pts[1] = {96, 52}; m_shaft_pts[2] = {46, 88};
        m_head_pts[0] = {34, 68}; m_head_pts[1] = {44, 94}; m_head_pts[2] = {64, 72};
    };
    auto set_sharp_right = [this]() {
        m_shaft_pt_count = 3;
        m_shaft_pts[0] = {54, 135}; m_shaft_pts[1] = {54, 52}; m_shaft_pts[2] = {104, 88};
        m_head_pts[0] = {86, 72}; m_head_pts[1] = {106, 94}; m_head_pts[2] = {116, 68};
    };
    auto set_straight = [this]() {
        m_shaft_pt_count = 2;
        m_shaft_pts[0] = {75, 135}; m_shaft_pts[1] = {75, 32};
        m_head_pts[0] = {48, 60}; m_head_pts[1] = {75, 30}; m_head_pts[2] = {102, 60};
    };
    auto set_uturn = [this]() {
        m_shaft_pt_count = 4;
        m_shaft_pts[0] = {98, 135};
        m_shaft_pts[1] = {98, 42};
        m_shaft_pts[2] = {52, 42};
        m_shaft_pts[3] = {52, 85};
        m_head_pts[0] = {34, 68};
        m_head_pts[1] = {52, 94};
        m_head_pts[2] = {70, 68};
    };

    switch (maneuver) {
        case dashcue_v1_Maneuver_MV_TURN_LEFT:
            set_left(); caption = "TURN LEFT"; break;
        case dashcue_v1_Maneuver_MV_SLIGHT_LEFT:
            set_slight_left(); caption = "KEEP LEFT"; break;
        case dashcue_v1_Maneuver_MV_SHARP_LEFT:
            set_sharp_left(); caption = "SHARP LEFT"; break;
        case dashcue_v1_Maneuver_MV_FORK_LEFT:
        case dashcue_v1_Maneuver_MV_RAMP_LEFT:
            set_slight_left(); caption = "EXIT LEFT"; break;
        case dashcue_v1_Maneuver_MV_TURN_RIGHT:
            set_right(); caption = "TURN RIGHT"; break;
        case dashcue_v1_Maneuver_MV_SLIGHT_RIGHT:
            set_slight_right(); caption = "KEEP RIGHT"; break;
        case dashcue_v1_Maneuver_MV_SHARP_RIGHT:
            set_sharp_right(); caption = "SHARP RIGHT"; break;
        case dashcue_v1_Maneuver_MV_FORK_RIGHT:
        case dashcue_v1_Maneuver_MV_RAMP_RIGHT:
            set_slight_right(); caption = "EXIT RIGHT"; break;
        case dashcue_v1_Maneuver_MV_UTURN:
            set_uturn(); caption = "U-TURN"; break;
        case dashcue_v1_Maneuver_MV_ROUNDABOUT:
            show_arc = true; caption = "ROUNDABOUT"; break;
        case dashcue_v1_Maneuver_MV_MERGE:
            set_straight(); caption = "MERGE"; break;
        case dashcue_v1_Maneuver_MV_ARRIVE_LEFT:
            show_flag = true;
            m_flag_pts[0] = {84, 135}; m_flag_pts[1] = {114, 135}; m_flag_pts[2] = {99, 135};
            m_flag_pts[3] = {99, 28}; m_flag_pts[4] = {45, 56}; m_flag_pts[5] = {99, 84};
            caption = "ON LEFT"; color = UI_COLOR_OK; break;
        case dashcue_v1_Maneuver_MV_ARRIVE_RIGHT:
            show_flag = true;
            m_flag_pts[0] = {36, 135}; m_flag_pts[1] = {66, 135}; m_flag_pts[2] = {51, 135};
            m_flag_pts[3] = {51, 28}; m_flag_pts[4] = {105, 56}; m_flag_pts[5] = {51, 84};
            caption = "ON RIGHT"; color = UI_COLOR_OK; break;
        case dashcue_v1_Maneuver_MV_ARRIVE:
            show_flag = true;
            m_flag_pts[0] = {36, 135}; m_flag_pts[1] = {66, 135}; m_flag_pts[2] = {51, 135};
            m_flag_pts[3] = {51, 28}; m_flag_pts[4] = {105, 56}; m_flag_pts[5] = {51, 84};
            caption = "ARRIVED"; color = UI_COLOR_OK; break;
        case dashcue_v1_Maneuver_MV_CONTINUE:
        case dashcue_v1_Maneuver_MV_DEPART:
            set_straight(); caption = "CONTINUE"; break;
        default:
            set_straight(); caption = "STRAIGHT"; break;
    }

    // Arrows only belong to an active route. Leaving them on the standby card
    // would compete with the compass for the same pixels.
    if (m_mode == MODE_ACTIVE_NAV || m_mode == MODE_CALCULATING) {
        if (m_arrow_flag) lv_obj_add_flag(m_arrow_flag, LV_OBJ_FLAG_HIDDEN);
        if (m_arrow_shaft) lv_obj_add_flag(m_arrow_shaft, LV_OBJ_FLAG_HIDDEN);
        if (m_arrow_head) lv_obj_add_flag(m_arrow_head, LV_OBJ_FLAG_HIDDEN);
        if (m_roundabout_arc) lv_obj_add_flag(m_roundabout_arc, LV_OBJ_FLAG_HIDDEN);

        if (show_flag) {
            if (m_arrow_flag) {
                lv_line_set_points(m_arrow_flag, m_flag_pts, 6);
                lv_obj_remove_flag(m_arrow_flag, LV_OBJ_FLAG_HIDDEN);
            }
        } else if (show_arc) {
            if (m_roundabout_arc) lv_obj_remove_flag(m_roundabout_arc, LV_OBJ_FLAG_HIDDEN);
            if (m_arrow_head) {
                m_head_pts[0] = {100, 36};
                m_head_pts[1] = {118, 50};
                m_head_pts[2] = {100, 62};
                lv_obj_set_style_line_color(m_arrow_head, UI_COLOR_ACCENT, 0);
                lv_line_set_points(m_arrow_head, m_head_pts, 3);
                lv_obj_remove_flag(m_arrow_head, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            if (m_arrow_shaft) {
                lv_obj_set_style_line_color(m_arrow_shaft, color, 0);
                lv_line_set_points(m_arrow_shaft, m_shaft_pts, m_shaft_pt_count);
                lv_obj_remove_flag(m_arrow_shaft, LV_OBJ_FLAG_HIDDEN);
            }
            if (m_arrow_head) {
                lv_obj_set_style_line_color(m_arrow_head, color, 0);
                lv_line_set_points(m_arrow_head, m_head_pts, 3);
                lv_obj_remove_flag(m_arrow_head, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    if (m_maneuver_caption) {
        lv_label_set_text(m_maneuver_caption, caption);
        lv_obj_set_style_text_color(m_maneuver_caption, color, 0);
    }
}

void UiScreenNav::setDistanceText(lv_obj_t *label, int32_t meters, const char *suffix) {
    if (!label) return;
    char buf[32];
    if (meters <= 0) {
        snprintf(buf, sizeof(buf), "NEXT");
    } else if (m_imperial) {
        const double miles = meters / 1609.344;
        if (miles < 0.2) {
            snprintf(buf, sizeof(buf), "%d ft%s", (int)(meters / 0.3048), suffix ? suffix : "");
        } else {
            snprintf(buf, sizeof(buf), "%.1f mi%s", miles, suffix ? suffix : "");
        }
    } else if (meters >= 1000) {
        snprintf(buf, sizeof(buf), "%.1f km%s", meters / 1000.0f, suffix ? suffix : "");
    } else {
        snprintf(buf, sizeof(buf), "%d m%s", (int)meters, suffix ? suffix : "");
    }
    lv_label_set_text(label, buf);
}

void UiScreenNav::update(const dashcue_v1_NavState &state, bool is_stale) {
    if (!m_screen) return;

    const bool calculating = (state.status == dashcue_v1_NavStatus_NAV_CALCULATING);
    const bool route_active = state.status == dashcue_v1_NavStatus_NAV_ACTIVE ||
                              state.status == dashcue_v1_NavStatus_NAV_REROUTING ||
                              state.status == dashcue_v1_NavStatus_NAV_OFF_ROUTE ||
                              state.status == dashcue_v1_NavStatus_NAV_ARRIVED;
    m_route_active = route_active;

    // Without a link there is no authoritative navigation state to render.
    // The offline card is the whole screen; stale data from the last trip must
    // not masquerade as live guidance.
    if (!m_connected) {
        setMode(MODE_OFFLINE);
        applyStale(false);
        return;
    }

    if (!route_active && !calculating) {
        setMode(MODE_STANDBY);
        updateCompass(state.heading_deg);
        updateSpeedometer(state.speed_kmh_x10, state.speed_limit_kmh);

        if (m_standby_street_label) {
            const char *street = state.street[0] ? state.street : "Driving standby";
            lv_label_set_text(m_standby_street_label, street);
        }
        if (m_standby_status_label) {
            lv_label_set_text(m_standby_status_label,
                              state.speed_kmh_x10 >= 0 ? "GPS: active" : "GPS: searching");
        }
        if (m_standby_accuracy_label) {
            if (state.gps_accuracy_m > 0.0f && state.gps_accuracy_m < 1000.0f) {
                char acc_buf[32];
                // ASCII "+/-" instead of U+00B1, which is absent from the font.
                snprintf(acc_buf, sizeof(acc_buf), "+/-%.0f m accuracy", state.gps_accuracy_m);
                lv_label_set_text(m_standby_accuracy_label, acc_buf);
            } else {
                lv_label_set_text(m_standby_accuracy_label, "Waiting for a GPS fix");
            }
        }
        m_confirm_token = state.route_id;
        applyStale(is_stale);
        return;
    }

    setMode(calculating ? MODE_CALCULATING : MODE_ACTIVE_NAV);

    if (calculating) {
        applyManeuver(dashcue_v1_Maneuver_MV_CONTINUE);
        if (m_distance_label) lv_label_set_text(m_distance_label, "...");
        if (m_street_label) {
            lv_label_set_text(m_street_label, "Calculating route");
            adapt_street_layout(m_street_label, m_next_street_label, "Calculating route");
        }
        if (m_next_street_label) lv_label_set_text(m_next_street_label, "");
        if (m_remaining_dist_label) lv_label_set_text(m_remaining_dist_label, "");
        if (m_eta_label) lv_label_set_text(m_eta_label, "Planning");
        m_confirm_token = state.route_id;
        applyStale(is_stale);
        return;
    }

    static uint32_t s_arrived_since_ms = 0;
    if (state.status == dashcue_v1_NavStatus_NAV_ARRIVED) {
        if (s_arrived_since_ms == 0) s_arrived_since_ms = millis();
        if (millis() - s_arrived_since_ms > 10000) {
            setMode(MODE_STANDBY);
            updateCompass(state.heading_deg);
            updateSpeedometer(state.speed_kmh_x10, state.speed_limit_kmh);
            applyStale(is_stale);
            return;
        }
        applyManeuver(dashcue_v1_Maneuver_MV_ARRIVE);
        if (m_distance_label) lv_label_set_text(m_distance_label, "HERE");
        if (m_street_label) {
            lv_label_set_text(m_street_label, state.street[0] ? state.street : "Destination reached");
            adapt_street_layout(m_street_label, m_next_street_label,
                                state.street[0] ? state.street : "Destination reached");
        }
        if (m_next_street_label) lv_label_set_text(m_next_street_label, "");
        if (m_remaining_dist_label) lv_label_set_text(m_remaining_dist_label, "Arrived");
        if (m_eta_label) {
            lv_label_set_text(m_eta_label, "Route ended");
            lv_obj_set_style_text_color(m_eta_label, UI_COLOR_OK, 0);
        }
        if (m_roundabout_badge) lv_obj_add_flag(m_roundabout_badge, LV_OBJ_FLAG_HIDDEN);
        m_confirm_token = state.route_id;
        applyStale(is_stale);
        return;
    }
    s_arrived_since_ms = 0;

    applyManeuver(state.next);

    if (m_roundabout_badge && m_roundabout_label) {
        if (state.roundabout_exit > 0) {
            char r_buf[16];
            snprintf(r_buf, sizeof(r_buf), "Exit %u", (unsigned)state.roundabout_exit);
            lv_label_set_text(m_roundabout_label, r_buf);
            lv_obj_remove_flag(m_roundabout_badge, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(m_roundabout_badge, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (m_distance_label) {
        if (state.dist_to_next_m == 0) lv_label_set_text(m_distance_label, "NEXT");
        else setDistanceText(m_distance_label, state.dist_to_next_m, nullptr);
    }

    if (m_street_label) {
        const char *street = state.street[0] ? state.street : "Follow route";
        lv_label_set_text(m_street_label, street);
        adapt_street_layout(m_street_label, m_next_street_label, street);
    }

    if (m_next_street_label) {
        if (state.following != dashcue_v1_Maneuver_MV_NONE && state.dist_to_following_m > 0) {
            char dist_str[24];
            char next_buf[48];
            if (m_imperial) {
                int32_t feet = (int32_t)(state.dist_to_following_m * 3.28084f);
                if (feet < 1000) snprintf(dist_str, sizeof(dist_str), "%d ft", (int)feet);
                else snprintf(dist_str, sizeof(dist_str), "%.1f mi",
                              state.dist_to_following_m / 1609.344f);
            } else {
                if (state.dist_to_following_m < 1000) {
                    snprintf(dist_str, sizeof(dist_str), "%d m", (int)state.dist_to_following_m);
                } else {
                    snprintf(dist_str, sizeof(dist_str), "%.1f km",
                             state.dist_to_following_m / 1000.0f);
                }
            }
            snprintf(next_buf, sizeof(next_buf), "then %s", dist_str);
            lv_label_set_text(m_next_street_label, next_buf);
        } else {
            lv_label_set_text(m_next_street_label, "");
        }
    }

    if (m_remaining_dist_label) {
        char rem_buf[32];
        if (m_imperial) {
            const double miles = state.remaining_m / 1609.344;
            if (miles < 0.2) {
                snprintf(rem_buf, sizeof(rem_buf), "%u ft left",
                         (unsigned)(state.remaining_m * 3.28084f));
            } else {
                snprintf(rem_buf, sizeof(rem_buf), "%.1f mi left", miles);
            }
        } else {
            if (state.remaining_m >= 1000) {
                snprintf(rem_buf, sizeof(rem_buf), "%.1f km left", state.remaining_m / 1000.0f);
            } else {
                snprintf(rem_buf, sizeof(rem_buf), "%u m left", (unsigned)state.remaining_m);
            }
        }
        lv_label_set_text(m_remaining_dist_label, rem_buf);
    }

    // ETA carries the same traffic band as the phone. The word is kept next to
    // the colour so a colour-blind driver is not asked to decode hue alone.
    const char *traffic_word = "";
    lv_color_t traffic_color = UI_COLOR_TEXT_DIM;
    switch (state.traffic) {
        case dashcue_v1_TrafficLevel_TL_CLEAR:
            traffic_word = " CLEAR"; traffic_color = UI_COLOR_OK; break;
        case dashcue_v1_TrafficLevel_TL_MODERATE:
            traffic_word = " MOD"; traffic_color = UI_COLOR_WARN; break;
        case dashcue_v1_TrafficLevel_TL_HEAVY:
            traffic_word = " HEAVY"; traffic_color = UI_COLOR_DANGER; break;
        case dashcue_v1_TrafficLevel_TL_SEVERE:
            traffic_word = " SEVERE"; traffic_color = UI_COLOR_DANGER; break;
        default:
            break;
    }

    if (m_eta_label) {
        char eta_buf[40];
        const uint32_t eta_min = state.remaining_s / 60;
        if (eta_min >= 60) {
            snprintf(eta_buf, sizeof(eta_buf), "ETA %uh %um%s", (unsigned)(eta_min / 60),
                     (unsigned)(eta_min % 60), traffic_word);
        } else {
            snprintf(eta_buf, sizeof(eta_buf), "ETA %u min%s", (unsigned)eta_min, traffic_word);
        }
        lv_label_set_text(m_eta_label, eta_buf);
        lv_obj_set_style_text_color(m_eta_label, traffic_color, 0);
    }

    m_confirm_token = state.route_id;
    applyStale(is_stale);
}

void UiScreenNav::onEndNavClicked(lv_event_t *e) {
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (s_last_touch_ms != 0 && now - s_last_touch_ms < 300) return;
    s_last_touch_ms = now;

    Serial.println("UI: nav End Route clicked (modal)");
    UiScreenNav *self = (UiScreenNav *)lv_event_get_user_data(e);
    if (!self) return;
    if (self->m_confirm_modal) {
        lv_obj_remove_flag(self->m_confirm_modal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(self->m_confirm_modal);
    }
}

void UiScreenNav::onModalCancelClicked(lv_event_t *e) {
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (s_last_touch_ms != 0 && now - s_last_touch_ms < 300) return;
    s_last_touch_ms = now;

    UiScreenNav *self = (UiScreenNav *)lv_event_get_user_data(e);
    if (!self) return;
    if (self->m_confirm_modal) lv_obj_add_flag(self->m_confirm_modal, LV_OBJ_FLAG_HIDDEN);
}

void UiScreenNav::onModalConfirmClicked(lv_event_t *e) {
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (s_last_touch_ms != 0 && now - s_last_touch_ms < 300) return;
    s_last_touch_ms = now;

    UiScreenNav *self = (UiScreenNav *)lv_event_get_user_data(e);
    if (!self) return;
    if (self->m_confirm_modal) lv_obj_add_flag(self->m_confirm_modal, LV_OBJ_FLAG_HIDDEN);

    UiToProtoMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_CMD_NAV;
    msg.data.nav.command_id = ui_next_command_id();
    msg.data.nav.action = dashcue_v1_NavCommand_NC_END_NAVIGATION;
    msg.data.nav.route_id = self->m_confirm_token;
    msg.data.nav.confirm_token = self->m_confirm_token;
    ipc_send_ui_to_proto(&msg, 0);
}

void UiScreenNav::onLaunchMapsClicked(lv_event_t *e) {
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (s_last_touch_ms != 0 && now - s_last_touch_ms < 300) return;
    s_last_touch_ms = now;
    Serial.println("UI: nav Launch Maps clicked");

    UiToProtoMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_CMD_NAV;
    msg.data.nav.command_id = ui_next_command_id();
    msg.data.nav.action = dashcue_v1_NavCommand_NC_REROUTE;
    ipc_send_ui_to_proto(&msg, 0);
}

void UiScreenNav::onPairClicked(lv_event_t *e) {
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (s_last_touch_ms != 0 && now - s_last_touch_ms < 400) return;
    s_last_touch_ms = now;

    UiScreenNav *self = (UiScreenNav *)lv_event_get_user_data(e);
    if (!self) return;
    Serial.println("UI: nav Start pairing clicked");

    UiToProtoMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_CMD_PAIR_PHONE;
    msg.data.pair_phone.timeout_s = 120;
    ipc_send_ui_to_proto(&msg, 0);

    if (self->m_offline_hint) {
        lv_label_set_text(self->m_offline_hint, "Pairing open for 2 min - use the DashCue app");
        lv_obj_set_style_text_color(self->m_offline_hint, UI_COLOR_OK, 0);
    }
}
