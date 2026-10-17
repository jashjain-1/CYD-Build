#ifndef DASHCUE_UI_SCREEN_NAV_H
#define DASHCUE_UI_SCREEN_NAV_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>
#include "protocol/generated/dashcue.pb.h"

class UiScreenNav {
public:
    UiScreenNav();

    void create(lv_obj_t *parent);
    void destroy();
    void show();
    void hide();

    void update(const dashcue_v1_NavState &state, bool is_stale);
    /** Distance units come from the phone's SettingsData. */
    void setImperial(bool imperial);

private:
    lv_obj_t *m_screen;
    lv_obj_t *m_maneuver_box;
    lv_obj_t *m_instruction_card;
    lv_obj_t *m_empty_label;

    // Turn arrows are drawn from LVGL primitives rather than a glyph: the
    // Montserrat fonts do not guarantee the FontAwesome turn symbols, and a
    // missing glyph used to leave the driver with a blank icon at the exact
    // moment a turn cue matters.
    lv_obj_t *m_arrow_shaft;
    lv_obj_t *m_arrow_head;
    lv_obj_t *m_arrow_flag;
    lv_obj_t *m_roundabout_arc;
    lv_obj_t *m_maneuver_caption;
    lv_point_precise_t m_shaft_pts[6];
    lv_point_precise_t m_head_pts[3];
    lv_point_precise_t m_flag_pts[6];
    uint32_t           m_shaft_pt_count;

    lv_obj_t *m_roundabout_badge;
    lv_obj_t *m_roundabout_label;

    lv_obj_t *m_distance_label;
    lv_obj_t *m_street_label;
    lv_obj_t *m_next_street_label;
    lv_obj_t *m_info_strip;
    lv_obj_t *m_eta_label;
    lv_obj_t *m_remaining_dist_label;
    lv_obj_t *m_end_nav_btn;
    lv_obj_t *m_stale_overlay;
    lv_obj_t *m_btn_launch_maps;

    // Confirm End modal dialog (two 128x64 buttons)
    lv_obj_t *m_confirm_modal;
    lv_obj_t *m_btn_modal_cancel;
    lv_obj_t *m_btn_modal_confirm;

    uint32_t m_confirm_token;
    bool m_imperial;

    void applyManeuver(dashcue_v1_Maneuver maneuver);
    void setContentVisible(bool route_active);
    void setDistanceText(lv_obj_t *label, int32_t meters, const char *suffix);

    static void onEndNavClicked(lv_event_t *e);
    static void onModalCancelClicked(lv_event_t *e);
    static void onModalConfirmClicked(lv_event_t *e);
    static void onLaunchMapsClicked(lv_event_t *e);
};

#endif // DASHCUE_UI_SCREEN_NAV_H
