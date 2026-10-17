#include "ui_manager.h"
#include "ui_theme.h"
#include "state/ipc_queues.h"
#include "state/app_state.h"
#include "board/board_hal.h"
#include "touch/touch_hal.h"

#ifndef DASHCUE_NATIVE_TEST
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
static uint32_t millis() { return 0; }
#endif

namespace {

#if !defined(DASHCUE_NATIVE_TEST)
// Measures the one SRAM budget this product has: BLE stack, UI objects, task
// stacks and LVGL all come out of the same ~300 KB. Printed per screen so an
// exhausted heap is diagnosable from the console (§16: >= 40 KB free).
static void log_heap(const char *stage) {
    Serial.printf("HEAP after ui:%-12s free=%u largest=%u min=%u\n",
                  stage,
                  (unsigned)esp_get_free_heap_size(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned)esp_get_minimum_free_heap_size());
    Serial.flush();
}
#else
static void log_heap(const char *stage) { (void)stage; }
#endif

#ifndef DASHCUE_NATIVE_TEST
static void ui_task_func(void *param) {
    (void)param;
    while (true) {
        uint32_t now_ms = millis();
        UiManager::instance().step(now_ms);
        vTaskDelay(pdMS_TO_TICKS(5)); // <= 5 ms loop per spec §8.4
    }
}
#endif

} // namespace

UiManager& UiManager::instance() {
    static UiManager s_instance;
    return s_instance;
}

UiManager::UiManager()
    : m_root(nullptr),
      m_current_screen(UI_SCREEN_SPLASH),
      m_initialized(false),
      m_last_state_sync_ms(0),
      m_media_stale(true),
      m_nav_stale(true),
      m_phone_stale(false),
      m_driving_locked(false),
      m_bonded(false),
      m_connected(false),
      m_speed_kmh_x10(-1),
      m_applied_settings_version(0) {}

bool UiManager::init() {
    if (m_initialized) return true;

    // Initialize shared styles (§16: eliminate per-object heap styling)
    ui_theme_init();

    m_root = lv_screen_active();
    lv_obj_set_style_bg_color(m_root, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(m_root, LV_OPA_COVER, 0);

    // Create top status bar and bottom tab bar
    m_status_bar.create(m_root);
    m_tab_bar.create(m_root, onTabSelected);

    // Album-matched chrome: the media screen owns the dominant colour, the
    // manager owns the bars, so it hands the screen a sink instead of the
    // screen reaching across. Fires on every accent settle (create + track
    // change), not per animation frame.
    m_screen_media.setAccentListener(onAlbumAccent, this);

    // Only the visible screen owns LVGL objects. Protocol state lives in
    // app_state, so inactive screens can be recreated from a locked snapshot.
    // Keeping all four functional screens alive exhausted the shared BLE heap.
    log_heap("ui init");

    m_initialized = true;

    // Boot directly into splash screen (<= 1.5s per spec)
    navigateTo(UI_SCREEN_SPLASH);

#ifndef DASHCUE_NATIVE_TEST
    // Spawn ui_task on Core 1 at priority 4 (spec §8.4)
    TaskHandle_t task_handle = nullptr;
    BaseType_t res = xTaskCreatePinnedToCore(
        ui_task_func,
        "ui_task",
        12288,
        nullptr,
        4, // Priority 4
        &task_handle,
        1  // Core 1
    );
    return (res == pdPASS);
#else
    return true;
#endif
}

void UiManager::onAlbumAccent(uint32_t ui_accent_rgb, void *ctx) {
    UiManager *self = (UiManager*)ctx;
    if (!self) return;
    self->m_status_bar.setAccent(ui_accent_rgb);
    self->m_tab_bar.setAccent(ui_accent_rgb);
    if (self->m_current_screen == UI_SCREEN_MEDIA) {
        self->m_status_bar.setBackdropColor(self->m_screen_media.getGradTopRgb());
    }
}

void UiManager::setThemeDark(bool dark) {
    ui_theme_set_dark(dark);
    if (m_root) {
        lv_obj_set_style_bg_color(m_root, UI_COLOR_BG, 0);
    }
    m_status_bar.refreshTheme();
    m_tab_bar.refreshTheme();
    m_screen_settings.setThemeDark(dark);
    if (m_current_screen == UI_SCREEN_MEDIA) {
        m_status_bar.setBackdropColor(m_screen_media.getGradTopRgb());
    } else {
        m_status_bar.setBackdropColor(ui_theme_get_color_bg_hex());
    }
    refreshCurrentScreen();
}

void UiManager::refreshCurrentScreen() {
    switch (m_current_screen) {
        case UI_SCREEN_MEDIA:
            m_screen_media.destroy();
            m_screen_media.create(m_root);
            break;
        case UI_SCREEN_NAV:
            m_screen_nav.destroy();
            m_screen_nav.create(m_root);
            break;
        case UI_SCREEN_SETTINGS:
            m_screen_settings.destroy();
            m_screen_settings.create(m_root);
            break;
        case UI_SCREEN_PAIRING:
            m_screen_pairing.destroy();
            m_screen_pairing.create(m_root);
            break;
        case UI_SCREEN_DIAG:
            m_screen_diag.destroy();
            m_screen_diag.create(m_root);
            break;
        default:
            break;
    }
}

void UiManager::navigateTo(UiScreenId screen) {
    if (!touch_hal_is_calibrated() &&
        (screen == UI_SCREEN_MEDIA || screen == UI_SCREEN_NAV ||
         screen == UI_SCREEN_SETTINGS)) {
        screen = UI_SCREEN_CALIBRATION;
    }
    if (screen == m_current_screen && m_current_screen != UI_SCREEN_SPLASH) {
        return;
    }

    // Release before allocating the replacement, including during LVGL events.
    switch (m_current_screen) {
        case UI_SCREEN_SPLASH:      m_screen_splash.destroy(); break;
        case UI_SCREEN_PAIRING:     m_screen_pairing.destroy(); break;
        case UI_SCREEN_CALIBRATION: m_screen_calibration.destroy(); break;
        case UI_SCREEN_SAFEMODE:    m_screen_safemode.destroy(); break;
        case UI_SCREEN_DIAG:        m_screen_diag.destroy(); break;

        case UI_SCREEN_MEDIA:       m_screen_media.destroy(); break;
        case UI_SCREEN_NAV:         m_screen_nav.destroy(); break;
        case UI_SCREEN_SETTINGS:    m_screen_settings.destroy(); break;
    }

    m_current_screen = screen;

    // 2. Show warm screens or create+show cold screens
    switch (m_current_screen) {
        case UI_SCREEN_SPLASH:
            m_tab_bar.setVisible(false);
            m_screen_splash.create(m_root);
            m_screen_splash.show();
            break;

        case UI_SCREEN_MEDIA: {
            m_tab_bar.setVisible(true);
            m_tab_bar.setActiveTab(UI_TAB_MEDIA);
            m_screen_media.create(m_root);
            dashcue_v1_MediaState state;
            app_state_get_media_snapshot(&state);
            m_screen_media.update(state, app_state_is_media_stale());
            m_screen_media.refreshArtwork();
            m_status_bar.setBackdropColor(m_screen_media.getGradTopRgb());
            m_screen_media.show();
            applySettingsToScreens();
            break;
        }

        case UI_SCREEN_NAV: {
            m_status_bar.setBackdropColor(ui_theme_get_color_bg_hex());
            m_tab_bar.setVisible(true);
            m_tab_bar.setActiveTab(UI_TAB_NAV);
            m_screen_nav.create(m_root);
            dashcue_v1_NavState state;
            app_state_get_nav_snapshot(&state);
            m_screen_nav.update(state, app_state_is_nav_stale());
            m_screen_nav.show();
            applySettingsToScreens();
            break;
        }

        case UI_SCREEN_SETTINGS:
            m_tab_bar.setVisible(true);
            m_tab_bar.setActiveTab(UI_TAB_MORE);
            m_screen_settings.create(m_root);
            m_screen_settings.setDrivingLock(app_state_is_driving_locked(),
                                             app_state_get_speed_kmh_x10());
            m_screen_settings.setPairingState(app_state_has_bonded_phone(), app_state_is_connected());
            m_screen_settings.show();
            break;

        case UI_SCREEN_PAIRING:
            m_tab_bar.setVisible(false);
            m_screen_pairing.create(m_root);
            m_screen_pairing.show();
            break;

        case UI_SCREEN_CALIBRATION:
            m_tab_bar.setVisible(false);
            m_screen_calibration.create(m_root);
            m_screen_calibration.show();
            break;

        case UI_SCREEN_SAFEMODE:
            m_tab_bar.setVisible(false);
            m_screen_safemode.create(m_root);
            m_screen_safemode.show();
            break;

        case UI_SCREEN_DIAG:
            m_tab_bar.setVisible(false);
            m_screen_diag.create(m_root);
            m_screen_diag.show();
            break;
    }

    if (m_root) {
        lv_obj_invalidate(m_root);
    }
    log_heap("screen change");
}

void UiManager::applySettingsToScreens() {
    dashcue_v1_SettingsData settings;
    app_state_get_settings(&settings);
    const bool imperial = (settings.units == dashcue_v1_UnitSystem_UN_IMPERIAL);
    m_screen_nav.setImperial(imperial);
    m_status_bar.setImperial(imperial);
    m_screen_settings.setUnits(imperial);
}

void UiManager::onTabSelected(UiTabId tab) {
    switch (tab) {
        case UI_TAB_MEDIA: UiManager::instance().navigateTo(UI_SCREEN_MEDIA); break;
        case UI_TAB_NAV:   UiManager::instance().navigateTo(UI_SCREEN_NAV); break;
        case UI_TAB_MORE:  UiManager::instance().navigateTo(UI_SCREEN_SETTINGS); break;
        default: break;
    }
}

void UiManager::step(uint32_t now_ms) {
    const uint32_t frame_start_ms = millis();

    // 1. Process all incoming protocol events from proto_to_ui_queue
    processIncomingEvents();
    refreshSafetyState(now_ms);

    // 2. Run active screen timers
    if (m_current_screen == UI_SCREEN_SPLASH) {
        m_screen_splash.update(now_ms);
    } else if (m_current_screen == UI_SCREEN_CALIBRATION) {
        m_screen_calibration.update();
    } else if (m_current_screen == UI_SCREEN_DIAG) {
        m_screen_diag.update(now_ms);
    }

    // 3. Drive LVGL timer handler (sole task invoking LVGL APIs per DEC-03, §8.4)
    lv_timer_handler();

    // 4. Report this iteration so the frame-time metrics on the diagnostics screen
    //    are measured numbers rather than placeholders (GATE-03 / TC-PERF-01).
    display_hal_record_ui_frame_ms(millis() - frame_start_ms);
}

void UiManager::refreshSafetyState(uint32_t now_ms) {
    if (now_ms - m_last_state_sync_ms < 100) return;
    m_last_state_sync_ms = now_ms;

    // Freshness/lock transitions originate in app_state; they are not guaranteed
    // to have a queued event (including a full queue or abrupt disconnect).
    const bool media_stale = app_state_is_media_stale();
    const bool nav_stale = app_state_is_nav_stale();
    const bool phone_stale = app_state_is_phone_stale();
    const bool locked = app_state_is_driving_locked();
    const int32_t speed = app_state_get_speed_kmh_x10();
    if (media_stale != m_media_stale && m_current_screen == UI_SCREEN_MEDIA) {
        dashcue_v1_MediaState state;
        app_state_get_media_snapshot(&state);
        m_screen_media.update(state, media_stale);
    }
    if (nav_stale != m_nav_stale && m_current_screen == UI_SCREEN_NAV) {
        dashcue_v1_NavState state;
        app_state_get_nav_snapshot(&state);
        m_screen_nav.update(state, nav_stale);
    }
    if (phone_stale != m_phone_stale) m_status_bar.setStale(phone_stale);
    if (speed != m_speed_kmh_x10) m_status_bar.setSpeed(speed);
    if (m_current_screen == UI_SCREEN_MEDIA) m_screen_media.tick(now_ms);
    if (locked != m_driving_locked || speed != m_speed_kmh_x10) {
        m_screen_settings.setDrivingLock(locked, speed);
    }
    // Bond/link changes are edge-triggered so a connected HUD does not keep
    // re-labeling the Bluetooth row every 100 ms.
    const bool bonded = app_state_has_bonded_phone();
    const bool connected = app_state_is_connected();
    if (bonded != m_bonded || connected != m_connected) {
        m_bonded = bonded;
        m_connected = connected;
        m_screen_settings.setPairingState(bonded, connected);
    }

    // Apply settings that arrived from the phone (or were just generated by
    // the HUD settings screen) on the UI task, never from the BLE task.
    dashcue_v1_SettingsData settings;
    app_state_get_settings(&settings);
    if (settings.version != 0 && settings.version != m_applied_settings_version) {
        const bool inverted =
            settings.orientation == dashcue_v1_ScreenOrientation_SO_LANDSCAPE_180;
        const bool imperial = (settings.units == dashcue_v1_UnitSystem_UN_IMPERIAL);

        if (settings.brightness_pct >= 10 && settings.brightness_pct <= 100) {
            board_set_backlight_brightness((uint8_t)settings.brightness_pct);
        }
        if (inverted != display_hal_get_rotation()) {
            display_hal_set_orientation(inverted);
        }
        const bool dark = (settings.theme != dashcue_v1_ThemeMode_TH_LIGHT);
        if (dark != ui_theme_is_dark()) {
            setThemeDark(dark);
        }

        m_status_bar.setImperial(imperial);
        m_screen_nav.setImperial(imperial);
        m_screen_settings.setUnits(imperial);
        if (m_current_screen == UI_SCREEN_SETTINGS) {
            if (settings.brightness_pct >= 10 && settings.brightness_pct <= 100) {
                m_screen_settings.setBrightness((uint8_t)settings.brightness_pct);
            }
            m_screen_settings.setOrientation(inverted);
        }
        if (m_current_screen == UI_SCREEN_NAV) {
            dashcue_v1_NavState nav;
            app_state_get_nav_snapshot(&nav);
            m_screen_nav.update(nav, app_state_is_nav_stale());
        }
        m_applied_settings_version = settings.version;
    }

    m_media_stale = media_stale;
    m_nav_stale = nav_stale;
    m_phone_stale = phone_stale;
    m_driving_locked = locked;
    m_speed_kmh_x10 = speed;

    if (m_current_screen == UI_SCREEN_PAIRING) {
        if (app_state_is_pairing_window_active()) {
            m_screen_pairing.setRemainingSeconds(app_state_get_pairing_remaining_s());
        } else {
            navigateTo(UI_SCREEN_MEDIA);
        }
    }
}

void UiManager::processIncomingEvents() {
    ProtoToUiMsg msg;
    // Drain up to 16 messages without blocking
    for (int i = 0; i < 16; i++) {
        if (!ipc_recv_proto_to_ui(&msg, 0)) {
            break;
        }

        switch (msg.type) {
            case IPC_EVT_MEDIA_STATE: {
                if (m_current_screen != UI_SCREEN_MEDIA) break;
                dashcue_v1_MediaState state;
                app_state_get_media_snapshot(&state);
                m_screen_media.update(state, app_state_is_media_stale());
                break;
            }

            case IPC_EVT_NAV_STATE: {
                dashcue_v1_NavState state;
                app_state_get_nav_snapshot(&state);
                m_status_bar.setSpeedLimit(state.speed_limit_kmh);
                if (m_current_screen != UI_SCREEN_NAV) break;
                m_screen_nav.update(state, app_state_is_nav_stale());
                break;
            }

            case IPC_EVT_PASSKEY:
                if (msg.data.pairing.remaining_seconds > 0) {
                    m_screen_pairing.setPasskey(msg.data.pairing.passkey);
                    m_screen_pairing.setRemainingSeconds(msg.data.pairing.remaining_seconds);
                } else {
                    m_screen_pairing.setPasskey(msg.data.passkey.passkey);
                    m_screen_pairing.setRemainingSeconds(msg.data.passkey.timeout_s);
                }
                navigateTo(UI_SCREEN_PAIRING);
                break;

            case IPC_EVT_PASSKEY_DISMISSED:
                if (m_current_screen == UI_SCREEN_PAIRING) {
                    navigateTo(UI_SCREEN_MEDIA);
                }
                break;

            case IPC_EVT_BLE_STATE:
                m_status_bar.setBleStatus(msg.data.ble_state.is_connected, false);
                break;

            case IPC_EVT_ARTWORK_READY:
                if (m_current_screen == UI_SCREEN_MEDIA) {
                    m_screen_media.refreshArtwork();
                }
                break;

            case IPC_EVT_DRIVING_LOCK:
                m_screen_settings.setDrivingLock(msg.data.driving_lock.locked,
                                                 msg.data.driving_lock.speed_kmh_x10);
                m_status_bar.setSpeed(msg.data.driving_lock.speed_kmh_x10);
                break;

            case IPC_EVT_FRESHNESS:
                m_status_bar.setStale(msg.data.freshness.phone_stale);
                break;

            case IPC_EVT_HEARTBEAT:
                if (msg.data.heartbeat.speed_kmh_x10 >= 0) {
                    m_status_bar.setSpeed(msg.data.heartbeat.speed_kmh_x10);
                }
                m_status_bar.setClockFromPhone(msg.data.heartbeat.phone_ts_ms,
                                               msg.data.heartbeat.tz_offset_min);
                m_status_bar.setBattery(msg.data.heartbeat.phone_battery_pct);
                break;

            default:
                break;
        }
    }
}
