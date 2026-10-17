#ifndef DASHCUE_UI_SCREEN_DIAG_H
#define DASHCUE_UI_SCREEN_DIAG_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

class UiScreenDiag {
public:
    UiScreenDiag();

    void create(lv_obj_t *parent);
    void destroy();
    void show();
    void hide();
    bool isCreated() const { return m_screen != nullptr; }

    void update(uint32_t now_ms);

private:
    lv_obj_t *m_screen;
    lv_obj_t *m_card;
    lv_obj_t *m_info_label;
    lv_obj_t *m_btn_back;

    uint32_t m_last_update_ms;

    static void onBackClicked(lv_event_t *e);
};

#endif // DASHCUE_UI_SCREEN_DIAG_H
