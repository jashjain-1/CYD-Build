#ifndef DASHCUE_UI_SCREEN_SPLASH_H
#define DASHCUE_UI_SCREEN_SPLASH_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

class UiScreenSplash {
public:
    UiScreenSplash();

    void create(lv_obj_t *parent);
    void destroy();
    void show();
    void hide();
    bool isCreated() const { return m_screen != nullptr; }

    /**
     * Periodic check to handle <= 1.5s timeout and auto-transition.
     */
    void update(uint32_t now_ms);

private:
    lv_obj_t *m_screen;
    lv_obj_t *m_logo_title;
    lv_obj_t *m_logo_subtitle;
    lv_obj_t *m_version_label;

    uint32_t m_start_ms;
    bool m_active;
};

#endif // DASHCUE_UI_SCREEN_SPLASH_H
