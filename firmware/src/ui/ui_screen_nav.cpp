#include "ui_screen_nav.h"
#include "ui_theme.h"
#include "state/ipc_queues.h"
#include "ui_command_id.h"
#include <stdio.h>
#ifndef DASHCUE_NATIVE_TEST
#include <Arduino.h>
#else
static uint32_t millis() { return 0; }
#endif

UiScreenNav::UiScreenNav()
    : m_screen(nullptr),
      m_maneuver_box(nullptr),
      m_instruction_card(nullptr),
      m_empty_label(nullptr),
      m_arrow_shaft(nullptr),
      m_arrow_head(nullptr),
      m_arrow_flag(nullptr),
      m_roundabout_arc(nullptr),
      m_maneuver_caption(nullptr),
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
      m_btn_launch_maps(nullptr),
      m_confirm_modal(nullptr),
      m_btn_modal_cancel(nullptr),
      m_btn_modal_confirm(nullptr),
      m_confirm_token(0),
      m_imperial(false) {
    m_shaft_pt_count = 2;
    m_shaft_pts[0] = {52, 78};
    m_shaft_pts[1] = {52, 22};
    m_head_pts[0] = {36, 38};
    m_head_pts[1] = {52, 20};
    m_head_pts[2] = {68, 38};
    // Triangular rally pennant flag with stable horizontal base (eliminates letter 'P')
    m_flag_pts[0] = {26, 78};
    m_flag_pts[1] = {46, 78};
    m_flag_pts[2] = {36, 78};
    m_flag_pts[3] = {36, 16};
    m_flag_pts[4] = {74, 35};
    m_flag_pts[5] = {36, 52};
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

    // 1. Maneuver card: 104x136. A drawn arrow fills the top, a plain-language
    //    caption names the turn (colour/glyph is never the only cue).
    m_maneuver_box = lv_obj_create(m_screen);
    lv_obj_set_size(m_maneuver_box, 104, 136);
    lv_obj_set_pos(m_maneuver_box, 8, 8);
    lv_obj_set_style_bg_color(m_maneuver_box, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(m_maneuver_box, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_border_width(m_maneuver_box, 1, 0);
    lv_obj_set_style_radius(m_maneuver_box, 10, 0);
    lv_obj_set_style_pad_all(m_maneuver_box, 0, 0);
    lv_obj_remove_flag(m_maneuver_box, LV_OBJ_FLAG_SCROLLABLE);

    m_arrow_shaft = lv_line_create(m_maneuver_box);
    lv_obj_set_style_line_width(m_arrow_shaft, 8, 0);
    lv_obj_set_style_line_rounded(m_arrow_shaft, true, 0);
    lv_obj_set_style_line_color(m_arrow_shaft, UI_COLOR_ACCENT, 0);
    lv_line_set_points(m_arrow_shaft, m_shaft_pts, m_shaft_pt_count);
    lv_obj_remove_flag(m_arrow_shaft, LV_OBJ_FLAG_CLICKABLE);

    m_arrow_head = lv_line_create(m_maneuver_box);
    lv_obj_set_style_line_width(m_arrow_head, 8, 0);
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
    lv_obj_set_size(m_roundabout_arc, 66, 66);
    lv_obj_set_pos(m_roundabout_arc, 19, 8);
    lv_arc_set_bg_angles(m_roundabout_arc, 35, 325);
    lv_arc_set_rotation(m_roundabout_arc, 0);
    lv_obj_set_style_arc_width(m_roundabout_arc, 9, LV_PART_MAIN);
    lv_obj_set_style_arc_color(m_roundabout_arc, UI_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(m_roundabout_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(m_roundabout_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_remove_flag(m_roundabout_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(m_roundabout_arc, LV_OBJ_FLAG_HIDDEN);

    m_maneuver_caption = lv_label_create(m_maneuver_box);
    lv_label_set_text(m_maneuver_caption, "STRAIGHT");
    lv_obj_set_style_text_color(m_maneuver_caption, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(m_maneuver_caption, UI_FONT_SM, 0);
    lv_obj_set_size(m_maneuver_caption, 96, 32);
    lv_obj_set_pos(m_maneuver_caption, 4, 84);
    lv_obj_set_style_text_align(m_maneuver_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(m_maneuver_caption, LV_LABEL_LONG_WRAP);

    m_roundabout_badge = lv_obj_create(m_maneuver_box);
    lv_obj_set_size(m_roundabout_badge, 52, 16);
    lv_obj_set_pos(m_roundabout_badge, 26, 114);
    lv_obj_set_style_bg_color(m_roundabout_badge, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_radius(m_roundabout_badge, 4, 0);
    lv_obj_set_style_pad_all(m_roundabout_badge, 0, 0);
    lv_obj_set_style_border_width(m_roundabout_badge, 0, 0);
    lv_obj_add_flag(m_roundabout_badge, LV_OBJ_FLAG_HIDDEN);

    m_roundabout_label = lv_label_create(m_roundabout_badge);
    lv_label_set_text(m_roundabout_label, "Exit 1");
    lv_obj_set_style_text_color(m_roundabout_label, UI_COLOR_BG, 0);
    lv_obj_set_style_text_font(m_roundabout_label, UI_FONT_SM, 0);
    lv_obj_center(m_roundabout_label);

    // 2. Instruction card: 194x136 at x: 120.
    m_instruction_card = lv_obj_create(m_screen);
    lv_obj_t *instruction_card = m_instruction_card;
    lv_obj_set_size(instruction_card, 194, 136);
    lv_obj_set_pos(instruction_card, 120, 8);
    lv_obj_set_style_bg_color(instruction_card, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(instruction_card, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_border_width(instruction_card, 1, 0);
    lv_obj_set_style_radius(instruction_card, 10, 0);
    lv_obj_set_style_pad_all(instruction_card, 0, 0);
    lv_obj_remove_flag(instruction_card, LV_OBJ_FLAG_SCROLLABLE);

    m_distance_label = lv_label_create(instruction_card);
    lv_label_set_text(m_distance_label, "350 m");
    lv_obj_set_style_text_color(m_distance_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_distance_label, UI_FONT_DIGITS_48, 0);
    lv_obj_set_pos(m_distance_label, 8, 0);
    lv_obj_set_size(m_distance_label, 180, 46);

    m_street_label = lv_label_create(instruction_card);
    lv_label_set_text(m_street_label, "Grand Avenue");
    lv_obj_set_style_text_color(m_street_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_street_label, UI_FONT_LG, 0);
    lv_obj_set_pos(m_street_label, 8, 46);
    lv_obj_set_size(m_street_label, 178, 28);
    lv_label_set_long_mode(m_street_label, LV_LABEL_LONG_DOT);

    m_next_street_label = lv_label_create(instruction_card);
    lv_label_set_text(m_next_street_label, "");
    lv_obj_set_style_text_color(m_next_street_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(m_next_street_label, UI_FONT_SM, 0);
    lv_obj_set_pos(m_next_street_label, 8, 68);
    lv_obj_set_size(m_next_street_label, 178, 18);

    // 3. Bottom strip: remaining distance/time plus End (>=44px driver touch target).
    m_info_strip = lv_obj_create(instruction_card);
    lv_obj_set_size(m_info_strip, 194, 48);
    lv_obj_set_pos(m_info_strip, 0, 88);
    lv_obj_set_style_bg_color(m_info_strip, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_border_width(m_info_strip, 0, 0);
    lv_obj_set_style_radius(m_info_strip, 0, 0);
    lv_obj_set_style_pad_all(m_info_strip, 0, 0);
    lv_obj_remove_flag(m_info_strip, LV_OBJ_FLAG_SCROLLABLE);

    m_remaining_dist_label = lv_label_create(m_info_strip);
    lv_label_set_text(m_remaining_dist_label, "12.4 km");
    lv_obj_set_style_text_color(m_remaining_dist_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(m_remaining_dist_label, UI_FONT_SM, 0);
    lv_obj_set_pos(m_remaining_dist_label, 6, 6);

    m_eta_label = lv_label_create(m_info_strip);
    lv_label_set_text(m_eta_label, "ETA 18 min");
    lv_obj_set_style_text_color(m_eta_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(m_eta_label, UI_FONT_SM, 0);
    lv_obj_set_pos(m_eta_label, 6, 26);

    m_end_nav_btn = lv_button_create(m_info_strip);
    lv_obj_set_size(m_end_nav_btn, 68, 44);
    lv_obj_align(m_end_nav_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(m_end_nav_btn, UI_COLOR_DANGER, 0);
    lv_obj_set_style_radius(m_end_nav_btn, 6, 0);
    lv_obj_set_style_pad_all(m_end_nav_btn, 0, 0);
    lv_obj_add_event_cb(m_end_nav_btn, onEndNavClicked, LV_EVENT_CLICKED, this);

    lv_obj_t *end_lbl = lv_label_create(m_end_nav_btn);
    lv_label_set_text(end_lbl, "End");
    lv_obj_set_style_text_color(end_lbl, UI_COLOR_BG, 0);
    lv_obj_set_style_text_font(end_lbl, UI_FONT_SM, 0);
    lv_obj_center(end_lbl);

    // Stale banner
    m_stale_overlay = lv_obj_create(m_screen);
    lv_obj_set_size(m_stale_overlay, UI_SCREEN_W, 24);
    lv_obj_set_pos(m_stale_overlay, 0, 0);
    lv_obj_set_style_bg_color(m_stale_overlay, UI_COLOR_WARN, 0);
    lv_obj_set_style_bg_opa(m_stale_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(m_stale_overlay, 0, 0);
    lv_obj_set_style_radius(m_stale_overlay, 0, 0);

    lv_obj_t *stale_text = lv_label_create(m_stale_overlay);
    lv_label_set_text(stale_text, LV_SYMBOL_WARNING " Navigation Stale - Searching for GPS");
    lv_obj_set_style_text_color(stale_text, UI_COLOR_BG, 0);
    lv_obj_set_style_text_font(stale_text, UI_FONT_SM, 0);
    lv_obj_center(stale_text);
    lv_obj_add_flag(m_stale_overlay, LV_OBJ_FLAG_HIDDEN);

    // Confirm End Modal Dialog (two 128x64 buttons, Rule 14)
    m_confirm_modal = lv_obj_create(parent);
    lv_obj_set_size(m_confirm_modal, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(m_confirm_modal, 0, 0);
    lv_obj_set_style_bg_color(m_confirm_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(m_confirm_modal, LV_OPA_80, 0);
    lv_obj_set_style_border_width(m_confirm_modal, 0, 0);
    lv_obj_set_style_radius(m_confirm_modal, 0, 0);
    lv_obj_remove_flag(m_confirm_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dialog_box = lv_obj_create(m_confirm_modal);
    lv_obj_set_size(dialog_box, 290, 150);
    lv_obj_center(dialog_box);
    lv_obj_set_style_bg_color(dialog_box, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(dialog_box, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_border_width(dialog_box, 2, 0);
    lv_obj_set_style_radius(dialog_box, 12, 0);
    lv_obj_remove_flag(dialog_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dialog_title = lv_label_create(dialog_box);
    lv_label_set_text(dialog_title, "End Current Route?");
    lv_obj_set_style_text_font(dialog_title, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(dialog_title, UI_COLOR_TEXT, 0);
    lv_obj_align(dialog_title, LV_ALIGN_TOP_MID, 0, 8);

    m_btn_modal_cancel = lv_button_create(dialog_box);
    lv_obj_set_size(m_btn_modal_cancel, 128, 64);
    lv_obj_align(m_btn_modal_cancel, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    lv_obj_set_style_bg_color(m_btn_modal_cancel, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_radius(m_btn_modal_cancel, 8, 0);
    lv_obj_add_event_cb(m_btn_modal_cancel, onModalCancelClicked, LV_EVENT_CLICKED, this);

    lv_obj_t *cancel_text = lv_label_create(m_btn_modal_cancel);
    lv_label_set_text(cancel_text, "Cancel");
    lv_obj_set_style_text_font(cancel_text, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(cancel_text, UI_COLOR_TEXT, 0);
    lv_obj_center(cancel_text);

    m_btn_modal_confirm = lv_button_create(dialog_box);
    lv_obj_set_size(m_btn_modal_confirm, 128, 64);
    lv_obj_align(m_btn_modal_confirm, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_obj_set_style_bg_color(m_btn_modal_confirm, UI_COLOR_DANGER, 0);
    lv_obj_set_style_radius(m_btn_modal_confirm, 8, 0);
    lv_obj_add_event_cb(m_btn_modal_confirm, onModalConfirmClicked, LV_EVENT_CLICKED, this);

    lv_obj_t *confirm_text = lv_label_create(m_btn_modal_confirm);
    lv_label_set_text(confirm_text, "End Route");
    lv_obj_set_style_text_font(confirm_text, UI_FONT_LG, 0);
    lv_obj_set_style_text_color(confirm_text, UI_COLOR_TEXT, 0);
    lv_obj_center(confirm_text);

    lv_obj_add_flag(m_confirm_modal, LV_OBJ_FLAG_HIDDEN);

    // Empty state: without an active route the cue cards would otherwise show
    // stale placeholder text (a fake "Follow Route" instruction).
    m_empty_label = lv_label_create(m_screen);
    lv_label_set_text(m_empty_label,
                      "NO ACTIVE ROUTE\n\n"
                      "Start navigation on your phone\n"
                      "or tap below to launch Google Maps.");
    lv_obj_set_size(m_empty_label, UI_SCREEN_W - 32, 76);
    lv_obj_set_pos(m_empty_label, 16, 16);
    lv_obj_set_style_text_align(m_empty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(m_empty_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(m_empty_label, UI_FONT_MD, 0);
    lv_label_set_long_mode(m_empty_label, LV_LABEL_LONG_WRAP);

    m_btn_launch_maps = lv_button_create(m_screen);
    lv_obj_set_size(m_btn_launch_maps, 200, 44);
    lv_obj_set_pos(m_btn_launch_maps, (UI_SCREEN_W - 200) / 2, 102);
    lv_obj_set_style_bg_color(m_btn_launch_maps, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_color(m_btn_launch_maps, UI_COLOR_SURFACE2, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(m_btn_launch_maps, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(m_btn_launch_maps, 1, 0);
    lv_obj_set_style_radius(m_btn_launch_maps, 8, 0);
    lv_obj_add_event_cb(m_btn_launch_maps, onLaunchMapsClicked, LV_EVENT_CLICKED, this);

    lv_obj_t *launch_lbl = lv_label_create(m_btn_launch_maps);
    lv_label_set_text(launch_lbl, LV_SYMBOL_GPS " Launch Google Maps");
    lv_obj_set_style_text_color(launch_lbl, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(launch_lbl, UI_FONT_SM, 0);
    lv_obj_center(launch_lbl);

    setContentVisible(false);
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
    m_imperial = imperial;
}

void UiScreenNav::setContentVisible(bool route_active) {
    if (!m_screen) return;
    if (route_active) {
        lv_obj_add_flag(m_empty_label, LV_OBJ_FLAG_HIDDEN);
        if (m_btn_launch_maps) lv_obj_add_flag(m_btn_launch_maps, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(m_maneuver_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(m_instruction_card, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(m_empty_label, LV_OBJ_FLAG_HIDDEN);
        if (m_btn_launch_maps) lv_obj_remove_flag(m_btn_launch_maps, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_maneuver_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_instruction_card, LV_OBJ_FLAG_HIDDEN);
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
        m_shaft_pts[0] = {62, 78}; m_shaft_pts[1] = {62, 48}; m_shaft_pts[2] = {26, 48};
        m_head_pts[0] = {42, 32}; m_head_pts[1] = {24, 48}; m_head_pts[2] = {42, 64};
    };
    auto set_right = [this]() {
        m_shaft_pt_count = 3;
        m_shaft_pts[0] = {42, 78}; m_shaft_pts[1] = {42, 48}; m_shaft_pts[2] = {78, 48};
        m_head_pts[0] = {62, 32}; m_head_pts[1] = {80, 48}; m_head_pts[2] = {62, 64};
    };
    auto set_slight_left = [this]() {
        m_shaft_pt_count = 3;
        m_shaft_pts[0] = {56, 78}; m_shaft_pts[1] = {56, 54}; m_shaft_pts[2] = {32, 26};
        m_head_pts[0] = {24, 40}; m_head_pts[1] = {30, 24}; m_head_pts[2] = {46, 30};
    };
    auto set_slight_right = [this]() {
        m_shaft_pt_count = 3;
        m_shaft_pts[0] = {48, 78}; m_shaft_pts[1] = {48, 54}; m_shaft_pts[2] = {72, 26};
        m_head_pts[0] = {58, 30}; m_head_pts[1] = {74, 24}; m_head_pts[2] = {80, 40};
    };
    auto set_sharp_left = [this]() {
        m_shaft_pt_count = 3;
        m_shaft_pts[0] = {64, 78}; m_shaft_pts[1] = {64, 34}; m_shaft_pts[2] = {32, 54};
        m_head_pts[0] = {22, 42}; m_head_pts[1] = {30, 58}; m_head_pts[2] = {42, 44};
    };
    auto set_sharp_right = [this]() {
        m_shaft_pt_count = 3;
        m_shaft_pts[0] = {40, 78}; m_shaft_pts[1] = {40, 34}; m_shaft_pts[2] = {72, 54};
        m_head_pts[0] = {62, 44}; m_head_pts[1] = {74, 58}; m_head_pts[2] = {82, 42};
    };
    auto set_straight = [this]() {
        m_shaft_pt_count = 2;
        m_shaft_pts[0] = {52, 78}; m_shaft_pts[1] = {52, 22};
        m_head_pts[0] = {36, 38}; m_head_pts[1] = {52, 20}; m_head_pts[2] = {68, 38};
    };
    auto set_uturn = [this]() {
        m_shaft_pt_count = 4;
        m_shaft_pts[0] = {66, 78};
        m_shaft_pts[1] = {66, 30};
        m_shaft_pts[2] = {38, 30};
        m_shaft_pts[3] = {38, 54};
        m_head_pts[0] = {26, 44};
        m_head_pts[1] = {38, 62};
        m_head_pts[2] = {50, 44};
    };

    switch (maneuver) {
        case dashcue_v1_Maneuver_MV_TURN_LEFT:
            set_left(); caption = "TURN\nLEFT"; break;
        case dashcue_v1_Maneuver_MV_SLIGHT_LEFT:
            set_slight_left(); caption = "KEEP\nLEFT"; break;
        case dashcue_v1_Maneuver_MV_SHARP_LEFT:
            set_sharp_left(); caption = "SHARP\nLEFT"; break;
        case dashcue_v1_Maneuver_MV_FORK_LEFT:
        case dashcue_v1_Maneuver_MV_RAMP_LEFT:
            set_slight_left(); caption = "EXIT\nLEFT"; break;

        case dashcue_v1_Maneuver_MV_TURN_RIGHT:
            set_right(); caption = "TURN\nRIGHT"; break;
        case dashcue_v1_Maneuver_MV_SLIGHT_RIGHT:
            set_slight_right(); caption = "KEEP\nRIGHT"; break;
        case dashcue_v1_Maneuver_MV_SHARP_RIGHT:
            set_sharp_right(); caption = "SHARP\nRIGHT"; break;
        case dashcue_v1_Maneuver_MV_FORK_RIGHT:
        case dashcue_v1_Maneuver_MV_RAMP_RIGHT:
            set_slight_right(); caption = "EXIT\nRIGHT"; break;

        case dashcue_v1_Maneuver_MV_UTURN:
            set_uturn(); caption = "U-TURN"; break;

        case dashcue_v1_Maneuver_MV_ROUNDABOUT:
            show_arc = true; caption = "ROUNDABOUT"; break;

        case dashcue_v1_Maneuver_MV_MERGE:
            set_straight(); caption = "MERGE"; break;

        case dashcue_v1_Maneuver_MV_ARRIVE_LEFT:
            show_flag = true;
            m_flag_pts[0] = {58, 78}; m_flag_pts[1] = {78, 78}; m_flag_pts[2] = {68, 78};
            m_flag_pts[3] = {68, 16}; m_flag_pts[4] = {30, 35}; m_flag_pts[5] = {68, 52};
            caption = "ON LEFT"; color = UI_COLOR_OK; break;

        case dashcue_v1_Maneuver_MV_ARRIVE_RIGHT:
            show_flag = true;
            m_flag_pts[0] = {26, 78}; m_flag_pts[1] = {46, 78}; m_flag_pts[2] = {36, 78};
            m_flag_pts[3] = {36, 16}; m_flag_pts[4] = {74, 35}; m_flag_pts[5] = {36, 52};
            caption = "ON RIGHT"; color = UI_COLOR_OK; break;

        case dashcue_v1_Maneuver_MV_ARRIVE:
            show_flag = true;
            m_flag_pts[0] = {26, 78}; m_flag_pts[1] = {46, 78}; m_flag_pts[2] = {36, 78};
            m_flag_pts[3] = {36, 16}; m_flag_pts[4] = {74, 35}; m_flag_pts[5] = {36, 52};
            caption = "ARRIVED"; color = UI_COLOR_OK; break;

        case dashcue_v1_Maneuver_MV_CONTINUE:
        case dashcue_v1_Maneuver_MV_DEPART:
            set_straight(); caption = "CONTINUE"; break;

        default:
            set_straight(); caption = "STRAIGHT"; break;
    }

    if (show_flag) {
        lv_obj_add_flag(m_arrow_shaft, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_arrow_head, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(m_arrow_flag, LV_OBJ_FLAG_HIDDEN);
        lv_line_set_points(m_arrow_flag, m_flag_pts, 6);
    } else {
        lv_obj_remove_flag(m_arrow_shaft, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(m_arrow_head, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_arrow_flag, LV_OBJ_FLAG_HIDDEN);
        lv_line_set_points(m_arrow_shaft, m_shaft_pts, m_shaft_pt_count);
        lv_line_set_points(m_arrow_head, m_head_pts, 3);
    }

    if (show_arc) {
        lv_obj_remove_flag(m_roundabout_arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_arrow_shaft, LV_OBJ_FLAG_HIDDEN);
        m_head_pts[0] = {74, 22}; m_head_pts[1] = {86, 34}; m_head_pts[2] = {72, 40};
        lv_line_set_points(m_arrow_head, m_head_pts, 3);
        lv_obj_set_style_line_color(m_arrow_head, UI_COLOR_ACCENT, 0);
    } else {
        lv_obj_add_flag(m_roundabout_arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_line_color(m_arrow_head, color, 0);
        lv_obj_set_style_line_color(m_arrow_shaft, color, 0);
    }

    lv_label_set_text(m_maneuver_caption, caption);
    lv_obj_set_style_text_color(m_maneuver_caption, color, 0);
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
    setContentVisible(route_active || calculating);
    if (!route_active && !calculating) {
        m_confirm_token = state.route_id;
        return;
    }
    if (calculating) {
        // Route is being computed: keep the cards, but do not claim a turn yet.
        applyManeuver(dashcue_v1_Maneuver_MV_CONTINUE);
        lv_label_set_text(m_distance_label, "...");
        lv_label_set_text(m_street_label, "Calculating route");
        if (m_next_street_label) lv_label_set_text(m_next_street_label, "");
        return;
    }

    static uint32_t s_arrived_since_ms = 0;
    if (state.status == dashcue_v1_NavStatus_NAV_ARRIVED) {
        if (s_arrived_since_ms == 0) s_arrived_since_ms = millis();
        if (millis() - s_arrived_since_ms > 10000) {
            setContentVisible(false);
            return;
        }
        applyManeuver(dashcue_v1_Maneuver_MV_ARRIVE);
        lv_label_set_text(m_distance_label, "HERE");
        lv_label_set_text(m_street_label, state.street[0] ? state.street : "Destination reached");
        if (m_next_street_label) lv_label_set_text(m_next_street_label, "");
        lv_label_set_text(m_remaining_dist_label, "Arrived");
        lv_label_set_text(m_eta_label, "Route ended");
        lv_obj_set_style_text_color(m_eta_label, UI_COLOR_OK, 0);
        lv_obj_add_flag(m_roundabout_badge, LV_OBJ_FLAG_HIDDEN);
        m_confirm_token = state.route_id;
        if (is_stale) {
            lv_obj_remove_flag(m_stale_overlay, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(m_stale_overlay, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    } else {
        s_arrived_since_ms = 0;
    }

    applyManeuver(state.next);

    if (state.roundabout_exit > 0) {
        char r_buf[16];
        snprintf(r_buf, sizeof(r_buf), "Exit %u", (unsigned)state.roundabout_exit);
        lv_label_set_text(m_roundabout_label, r_buf);
        lv_obj_remove_flag(m_roundabout_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(m_roundabout_badge, LV_OBJ_FLAG_HIDDEN);
    }

    if (m_distance_label) {
        if (state.dist_to_next_m == 0) {
            lv_label_set_text(m_distance_label, "NEXT");
        } else {
            setDistanceText(m_distance_label, state.dist_to_next_m, nullptr);
        }
    }

    if (m_street_label) {
        lv_label_set_text(m_street_label, state.street[0] ? state.street : "Follow Route");
    }

    if (m_next_street_label) {
        if (state.following != dashcue_v1_Maneuver_MV_NONE && state.dist_to_following_m > 0) {
            char dist_str[24];
            char next_buf[48];
            if (m_imperial) {
                int32_t feet = (int32_t)(state.dist_to_following_m * 3.28084f);
                if (feet < 1000) {
                    snprintf(dist_str, sizeof(dist_str), "%d ft", (int)feet);
                } else {
                    snprintf(dist_str, sizeof(dist_str), "%.1f mi", state.dist_to_following_m / 1609.344f);
                }
            } else {
                if (state.dist_to_following_m < 1000) {
                    snprintf(dist_str, sizeof(dist_str), "%d m", (int)state.dist_to_following_m);
                } else {
                    snprintf(dist_str, sizeof(dist_str), "%.1f km", state.dist_to_following_m / 1000.0f);
                }
            }
            snprintf(next_buf, sizeof(next_buf), "then %s", dist_str);
            lv_label_set_text(m_next_street_label, next_buf);
        } else {
            lv_label_set_text(m_next_street_label, "");
        }
    }

    if (m_remaining_dist_label) {
        char rem_buf[24];
        if (m_imperial) {
            const double miles = state.remaining_m / 1609.344;
            if (miles < 0.2) {
                snprintf(rem_buf, sizeof(rem_buf), "%u ft left", (unsigned)(state.remaining_m * 3.28084f));
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

    char eta_buf[40];
    uint32_t eta_min = state.remaining_s / 60;
    if (eta_min >= 60) {
        snprintf(eta_buf, sizeof(eta_buf), "ETA %uh %um%s", (unsigned)(eta_min / 60),
                 (unsigned)(eta_min % 60), traffic_word);
    } else {
        snprintf(eta_buf, sizeof(eta_buf), "ETA %u min%s", (unsigned)eta_min, traffic_word);
    }
    lv_label_set_text(m_eta_label, eta_buf);
    lv_obj_set_style_text_color(m_eta_label, traffic_color, 0);

    m_confirm_token = state.route_id;

    if (is_stale) {
        lv_obj_remove_flag(m_stale_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(m_stale_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiScreenNav::onEndNavClicked(lv_event_t *e) {
    static uint32_t s_last_touch_ms = 0;
    const uint32_t now = millis();
    if (now - s_last_touch_ms < 300) return;
    s_last_touch_ms = now;

    UiScreenNav *self = (UiScreenNav*)lv_event_get_user_data(e);
    if (!self) return;
    if (self->m_confirm_modal) {
        lv_obj_remove_flag(self->m_confirm_modal, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiScreenNav::onModalCancelClicked(lv_event_t *e) {
    UiScreenNav *self = (UiScreenNav*)lv_event_get_user_data(e);
    if (!self) return;
    if (self->m_confirm_modal) {
        lv_obj_add_flag(self->m_confirm_modal, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiScreenNav::onModalConfirmClicked(lv_event_t *e) {
    UiScreenNav *self = (UiScreenNav*)lv_event_get_user_data(e);
    if (!self) return;

    if (self->m_confirm_modal) {
        lv_obj_add_flag(self->m_confirm_modal, LV_OBJ_FLAG_HIDDEN);
    }

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
    if (now - s_last_touch_ms < 300) return;
    s_last_touch_ms = now;

    UiToProtoMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_CMD_NAV;
    msg.data.nav.command_id = ui_next_command_id();
    msg.data.nav.action = dashcue_v1_NavCommand_NC_REROUTE;
    ipc_send_ui_to_proto(&msg, 0);
}

