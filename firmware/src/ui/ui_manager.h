#ifndef DASHCUE_UI_MANAGER_H
#define DASHCUE_UI_MANAGER_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

#include "ui_status_bar.h"
#include "ui_tab_bar.h"
#include "ui_screen_media.h"
#include "ui_screen_nav.h"
#include "ui_screen_settings.h"
#include "ui_screen_pairing.h"
#include "ui_screen_calibration.h"
#include "ui_screen_safemode.h"
#include "ui_screen_splash.h"
#include "ui_screen_diag.h"

#include "display/display_hal.h"

class UiManager {
public:
    static UiManager& instance();

    /**
     * Initialize shared bars and the visible screen, then spawn ui_task.
     */
    bool init();

    /**
     * Navigate to target screen.
     */
    void navigateTo(UiScreenId screen);
    UiScreenId getCurrentScreen() const { return m_current_screen; }

    /**
     * Periodic step called from ui_task loop.
     * Dequeues protocol events, runs active screen updates, calls lv_timer_handler().
     */
    void step(uint32_t now_ms);

    UiStatusBar& getStatusBar() { return m_status_bar; }
    UiTabBar& getTabBar() { return m_tab_bar; }
    UiScreenMedia& getScreenMedia() { return m_screen_media; }
    UiScreenNav& getScreenNav() { return m_screen_nav; }
    UiScreenSettings& getScreenSettings() { return m_screen_settings; }
    /** Applied whenever a screen is (re)created so units match the phone. */
    void applySettingsToScreens();
    UiScreenPairing& getScreenPairing() { return m_screen_pairing; }
    UiScreenCalibration& getScreenCalibration() { return m_screen_calibration; }
    UiScreenSafeMode& getScreenSafeMode() { return m_screen_safemode; }
    UiScreenSplash& getScreenSplash() { return m_screen_splash; }
    UiScreenDiag& getScreenDiag() { return m_screen_diag; }

private:
    UiManager();

    lv_obj_t *m_root;
    UiScreenId m_current_screen;

    UiStatusBar          m_status_bar;
    UiTabBar             m_tab_bar;
    UiScreenMedia        m_screen_media;
    UiScreenNav          m_screen_nav;
    UiScreenSettings     m_screen_settings;
    UiScreenPairing      m_screen_pairing;
    UiScreenCalibration  m_screen_calibration;
    UiScreenSafeMode     m_screen_safemode;
    UiScreenSplash       m_screen_splash;
    UiScreenDiag         m_screen_diag;

    bool m_initialized;
    uint32_t m_last_state_sync_ms;
    bool m_media_stale;
    bool m_nav_stale;
    bool m_phone_stale;
    bool m_driving_locked;
    bool m_bonded;
    bool m_connected;
    int32_t m_speed_kmh_x10;
    uint32_t m_applied_settings_version;

    void processIncomingEvents();
    void refreshSafetyState(uint32_t now_ms);
    static void onTabSelected(UiTabId tab);
    /** Pushes the media screen's album accent out to the chrome bars. */
    static void onAlbumAccent(uint32_t ui_accent_rgb, void *ctx);
};

#endif // DASHCUE_UI_MANAGER_H
