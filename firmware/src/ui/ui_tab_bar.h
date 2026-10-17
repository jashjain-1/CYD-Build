#ifndef DASHCUE_UI_TAB_BAR_H
#define DASHCUE_UI_TAB_BAR_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

enum UiTabId {
    UI_TAB_MEDIA = 0,
    UI_TAB_NAV = 1,
    UI_TAB_MORE = 2,
    UI_TAB_COUNT = 3
};

typedef void (*UiTabSelectCallback)(UiTabId tab);

class UiTabBar {
public:
    UiTabBar();

    void create(lv_obj_t *parent, UiTabSelectCallback cb);
    void setActiveTab(UiTabId tab);
    UiTabId getActiveTab() const { return m_active_tab; }
    void setVisible(bool visible);

    /**
     * Recolours the active tab (label + 3 px underline) with the current album
     * accent. Inactive tabs stay dim grey, so the tab bar still reads as a
     * navigation landmark rather than turning into album art.
     */
    void setAccent(uint32_t rgb);

private:
    lv_obj_t *m_container;
    lv_obj_t *m_buttons[UI_TAB_COUNT];
    lv_obj_t *m_labels[UI_TAB_COUNT];
    lv_obj_t *m_indicators[UI_TAB_COUNT];
    UiTabId m_active_tab;
    UiTabSelectCallback m_callback;
    uint32_t m_accent; // packed 0xRRGGBB, brand amber until an album arrives

    static void onTabClicked(lv_event_t *e);
};

#endif // DASHCUE_UI_TAB_BAR_H
