#ifndef DASHCUE_UI_STATUS_BAR_H
#define DASHCUE_UI_STATUS_BAR_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

class UiStatusBar {
public:
    UiStatusBar();

    void create(lv_obj_t *parent);

    void setBleStatus(bool connected, bool pairing);
    void setPhoneName(const char *name);
    void setClock(const char *time_str);
    void setClockFromPhone(uint64_t epoch_ms, int32_t tz_offset_min);
    void setBattery(uint32_t percent);
    void setSpeed(int32_t speed_kmh_x10);
    void setSpeedLimit(uint32_t limit_kmh);
    void setStale(bool stale);
    void setImperial(bool imperial);

    /**
     * Touch layer fault: the controller answers but reports no real contact.
     * Shown as a red TOUCH badge so "the screen ignores me" is explained on the
     * device instead of only in the serial log.
     */
    void setTouchFault(bool faulted);

    /**
     * Tints the bar's accent surfaces (linked-state glyph and wordmark, speed
     * readout) with the current album colour. Pass the *lifted* accent that
     * clears contrast on the near-black bar, not the raw album hue.
     */
    void setAccent(uint32_t rgb);
    void setBackdropColor(uint32_t rgb);
    void refreshTheme(void);
    lv_obj_t* getContainer() const { return m_container; }

private:
    lv_obj_t *m_container;
    lv_obj_t *m_ble_icon;
    lv_obj_t *m_phone_label;
    lv_obj_t *m_speed_label;
    lv_obj_t *m_limit_label;
    lv_obj_t *m_clock_label;
    lv_obj_t *m_battery_label;
    lv_obj_t *m_stale_badge;
    lv_obj_t *m_touch_badge;

    uint32_t m_accent;   // packed 0xRRGGBB, brand amber until an album arrives
    bool m_connected;
    bool m_pairing;
    bool m_imperial;
    int32_t m_last_speed_kmh_x10;
    bool m_touch_fault;
};

#endif // DASHCUE_UI_STATUS_BAR_H
