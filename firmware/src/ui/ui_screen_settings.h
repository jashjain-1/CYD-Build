#ifndef DASHCUE_UI_SCREEN_SETTINGS_H
#define DASHCUE_UI_SCREEN_SETTINGS_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

class UiScreenSettings {
public:
    UiScreenSettings();

    void create(lv_obj_t *parent);
    void destroy();
    void show();
    void hide();

    void setDrivingLock(bool locked, int32_t speed_kmh_x10);
    void setOrientation(bool inverted);
    void setBrightness(uint8_t percent);
    void setUnits(bool imperial);
    /** Reflects the real bond/link state; the Pair action disappears once a
     *  phone is bonded, because re-opening a pairing window does not pair. */
    void setPairingState(bool bonded, bool connected);

private:
    lv_obj_t *m_screen;
    lv_obj_t *m_list;

    // Driving lock overlay banner
    lv_obj_t *m_lock_banner;
    lv_obj_t *m_lock_label;

    // 48px List Rows
    lv_obj_t *m_row_brightness;
    lv_obj_t *m_btn_bri_down;
    lv_obj_t *m_btn_bri_up;
    lv_obj_t *m_lbl_brightness;

    lv_obj_t *m_row_theme;
    lv_obj_t *m_btn_theme;
    lv_obj_t *m_lbl_theme;

    lv_obj_t *m_row_units;
    lv_obj_t *m_btn_units;
    lv_obj_t *m_lbl_units;

    lv_obj_t *m_row_orient;
    lv_obj_t *m_btn_orient;
    lv_obj_t *m_lbl_orient;

    lv_obj_t *m_row_calib;
    lv_obj_t *m_btn_calib;

    lv_obj_t *m_row_pair;
    lv_obj_t *m_lbl_pair_title;
    lv_obj_t *m_btn_pair;
    lv_obj_t *m_btn_forget;

    lv_obj_t *m_row_diag;
    lv_obj_t *m_btn_diag;

    lv_obj_t *m_row_about;
    lv_obj_t *m_lbl_about;

    bool m_driving_locked;
    bool m_bonded;
    bool m_connected;
    bool m_inverted;
    bool m_dark_theme;
    bool m_imperial;
    uint8_t m_brightness;
    bool m_sync_enabled;
    uint32_t m_settings_version;

    void updateLockState();
    void sendSettings();

    static void onBriDownClicked(lv_event_t *e);
    static void onBriUpClicked(lv_event_t *e);
    static void onThemeClicked(lv_event_t *e);
    static void onUnitsClicked(lv_event_t *e);
    static void onOrientClicked(lv_event_t *e);
    static void onCalibClicked(lv_event_t *e);
    static void onPairClicked(lv_event_t *e);
    static void onForgetClicked(lv_event_t *e);
    static void onDiagClicked(lv_event_t *e);
};

#endif // DASHCUE_UI_SCREEN_SETTINGS_H
