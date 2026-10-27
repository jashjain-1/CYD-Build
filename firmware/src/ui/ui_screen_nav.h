#ifndef DASHCUE_UI_SCREEN_NAV_H
#define DASHCUE_UI_SCREEN_NAV_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>
#include "protocol/generated/dashcue.pb.h"

/**
 * Nav / cockpit screen.
 *
 * Three mutually exclusive presentations:
 *
 *   OFFLINE   no phone link. A deliberate, centred "pair your phone" card.
 *             The screen never renders a dead gauge or a fake heading.
 *   STANDBY   phone linked, no active route. A real compass rose plus a live
 *             speed cluster (GPS speed, speed-limit disc, street, fix quality).
 *   ACTIVE    turn-by-turn cue mirrored from Maps/Waze.
 *
 * Heading convention on the wire: NavState.heading_deg is degrees clockwise
 * from true north, or < 0 when the phone has neither a GPS course nor a
 * trustworthy magnetometer heading. A negative heading hides the needle and
 * shows an em dash; it is never rendered as "N 0".
 */
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

    /** Link state drives the offline presentation. */
    void setLinkState(bool connected, bool bonded);

private:
    lv_obj_t *m_screen;
    lv_obj_t *m_maneuver_box;
    lv_obj_t *m_instruction_card;

    // Navigation mode
    enum Mode { MODE_OFFLINE, MODE_STANDBY, MODE_ACTIVE_NAV, MODE_CALCULATING };
    Mode m_mode;
    bool m_connected;
    bool m_bonded;
    bool m_route_active;
    bool m_has_heading;

    // OFFLINE card (full workspace, centred)
    lv_obj_t *m_offline_card;
    lv_obj_t *m_offline_icon;
    lv_obj_t *m_offline_title;
    lv_obj_t *m_offline_body;
    lv_obj_t *m_offline_hint;
    lv_obj_t *m_offline_pair_btn;

    // STANDBY: compass rose (inside the maneuver card)
    lv_obj_t *m_compass_ring;
    lv_obj_t *m_compass_needle;   // north half, accent
    lv_obj_t *m_compass_tail;     // south half, dim
    lv_obj_t *m_compass_hub;
    lv_obj_t *m_compass_heading_label;
    lv_obj_t *m_compass_sub_label;
    lv_obj_t *m_cardinal_labels[4];
    lv_obj_t *m_tick_lines[4];
    lv_point_precise_t m_needle_pts[2];
    lv_point_precise_t m_tail_pts[2];
    lv_point_precise_t m_tick_pts[4][2];

    // STANDBY: speed cluster (inside the instruction card)
    lv_obj_t *m_standby_header;
    lv_obj_t *m_standby_link_dot;
    lv_obj_t *m_standby_street_label;
    lv_obj_t *m_standby_speed_digits;
    lv_obj_t *m_standby_speed_unit;
    lv_obj_t *m_speed_bar;
    lv_obj_t *m_speed_limit_badge;
    lv_obj_t *m_speed_limit_val;
    lv_obj_t *m_standby_status_label;
    lv_obj_t *m_standby_accuracy_label;
    lv_obj_t *m_standby_maps_btn;

    void setMode(Mode mode);
    void updateCompass(float heading_deg);
    void updateSpeedometer(int32_t speed_kmh_x10, uint32_t speed_limit_kmh);

    // ACTIVE navigation: turn arrows drawn from LVGL primitives rather than a
    // glyph (the Montserrat fonts do not guarantee the FontAwesome turn
    // symbols, and a missing glyph leaves the driver with a blank icon at the
    // exact moment a turn cue matters).
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

    // Confirm End modal dialog (two driver buttons)
    lv_obj_t *m_confirm_modal;
    lv_obj_t *m_btn_modal_cancel;
    lv_obj_t *m_btn_modal_confirm;

    uint32_t m_confirm_token;
    bool m_imperial;

    void applyManeuver(dashcue_v1_Maneuver maneuver);
    void setDistanceText(lv_obj_t *label, int32_t meters, const char *suffix);
    void layoutStandby();
    void layoutActive();
    void applyStale(bool stale);

    static void onEndNavClicked(lv_event_t *e);
    static void onModalCancelClicked(lv_event_t *e);
    static void onModalConfirmClicked(lv_event_t *e);
    static void onLaunchMapsClicked(lv_event_t *e);
    static void onPairClicked(lv_event_t *e);
};

#endif // DASHCUE_UI_SCREEN_NAV_H
