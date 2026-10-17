#ifndef DASHCUE_UI_SCREEN_PAIRING_H
#define DASHCUE_UI_SCREEN_PAIRING_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

class UiScreenPairing {
public:
    UiScreenPairing();

    void create(lv_obj_t *parent);
    void destroy();
    void show();
    void hide();
    bool isCreated() const { return m_screen != nullptr; }

    void setPasskey(uint32_t passkey);
    void setRemainingSeconds(uint32_t seconds);

private:
    lv_obj_t *m_screen;
    lv_obj_t *m_card;
    lv_obj_t *m_passkey_label;
    lv_obj_t *m_countdown_label;
    lv_obj_t *m_cancel_btn;

    uint32_t m_passkey;
    uint32_t m_seconds_remaining;
    bool m_has_passkey;

    static void onCancelClicked(lv_event_t *e);
};

#endif // DASHCUE_UI_SCREEN_PAIRING_H
