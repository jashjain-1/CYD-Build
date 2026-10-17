#ifndef DASHCUE_UI_SCREEN_SAFEMODE_H
#define DASHCUE_UI_SCREEN_SAFEMODE_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

class UiScreenSafeMode {
public:
    UiScreenSafeMode();

    void create(lv_obj_t *parent);
    void destroy();
    void show();
    void hide();
    bool isCreated() const { return m_screen != nullptr; }

    void setCrashDetails(uint32_t crash_count, uint32_t reset_reason);

private:
    lv_obj_t *m_screen;
    lv_obj_t *m_card;
    lv_obj_t *m_details_label;
    lv_obj_t *m_btn_calib;
    lv_obj_t *m_btn_factory_reset;
    lv_obj_t *m_btn_reboot;

    uint32_t m_crash_count;
    uint32_t m_reset_reason;

    static void onCalibClicked(lv_event_t *e);
    static void onResetClicked(lv_event_t *e);
    static void onRebootClicked(lv_event_t *e);
};

#endif // DASHCUE_UI_SCREEN_SAFEMODE_H
