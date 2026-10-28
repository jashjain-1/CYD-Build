#include "ui_manager.h"
#include "ui_theme.h"
#include "artwork_store.h"
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
        uint32_t next_call = UiManager::instance().step(now_ms);
        uint32_t sleep_ms = constrain(next_call, 4u, 30u);
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
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
        8192, // measured high-water 4,272 B on 2026-10-20 (was 12,288)
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
#if !defined(DASHCUE_NO_PAIRING)
        case UI_SCREEN_PAIRING:
            m_screen_pairing.destroy();
            m_screen_pairing.create(m_root);
            break;
#endif
        case UI_SCREEN_DIAG:
            m_screen_diag.destroy();
            m_screen_diag.create(m_root);
            break;
        default:
            break;
    }
}

void UiManager::navigateTo(UiScreenId screen) {
    // Uncalibrated coordinates must never drive operational controls, so the
    // functional screens redirect to the wizard. A *faulted* touch layer is the
    // one exception: the wizard needs taps that cannot arrive, and trapping the
    // HUD there turns a broken touch panel into a broken product. The fault is
    // measured in touch_hal (controller answering, every press saturated), not
    // guessed from a timeout, and the status bar carries the indicator.
    if (!touch_hal_is_calibrated() && !touch_hal_is_faulted() &&
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
#if !defined(DASHCUE_NO_PAIRING)
        case UI_SCREEN_PAIRING:     m_screen_pairing.destroy(); break;
#endif
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
            m_screen_media.show();
            applySettingsToScreens();
            break;
        }

        case UI_SCREEN_NAV: {
            m_tab_bar.setVisible(true);
            m_tab_bar.setActiveTab(UI_TAB_NAV);
            m_screen_nav.create(m_root);
            m_screen_nav.setLinkState(m_connected, m_bonded);
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

#if !defined(DASHCUE_NO_PAIRING)
        case UI_SCREEN_PAIRING:
            m_tab_bar.setVisible(false);
            m_screen_pairing.create(m_root);
            m_screen_pairing.show();
            break;
#endif

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

    if (m_status_bar.getContainer()) {
        lv_obj_move_foreground(m_status_bar.getContainer());
    }
    if (m_tab_bar.getContainer()) {
        lv_obj_move_foreground(m_tab_bar.getContainer());
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
    Serial.printf("UI: onTabSelected -> tab %d\n", (int)tab);
    switch (tab) {
        case UI_TAB_MEDIA: UiManager::instance().navigateTo(UI_SCREEN_MEDIA); break;
        case UI_TAB_NAV:   UiManager::instance().navigateTo(UI_SCREEN_NAV); break;
        case UI_TAB_MORE:  UiManager::instance().navigateTo(UI_SCREEN_SETTINGS); break;
        default: break;
    }
}

uint32_t UiManager::step(uint32_t now_ms) {
    const uint32_t frame_start_ms = millis();

    // 1. Process all incoming protocol events from proto_to_ui_queue
    processIncomingEvents();
    refreshSafetyState(now_ms);

    // 1b. Fallback input. With the touch overlay unable to sense position, the
    //     BOOT button is the only control the operator has, so it steps through
    //     the three functional tabs. One step per press, 250 ms lockout: the
    //     button has no hardware debounce worth trusting and this loop runs at
    //     200 Hz.
    {
        static bool s_btn_prev = false;
        static uint32_t s_btn_edge_ms = 0;
        const bool btn = board_button_pressed();
        if (btn && !s_btn_prev && (now_ms - s_btn_edge_ms) >= 250) {
            s_btn_edge_ms = now_ms;
            cycleTab();
            return 4;  // the screen this step was working on is gone; wake up promptly to draw new tab
        }
        s_btn_prev = btn;
    }

    // 2. Run active screen timers
    if (m_current_screen == UI_SCREEN_SPLASH) {
        m_screen_splash.update(now_ms);
    } else if (m_current_screen == UI_SCREEN_CALIBRATION) {
        m_screen_calibration.update();
    } else if (m_current_screen == UI_SCREEN_DIAG) {
        m_screen_diag.update(now_ms);
    }

    // 3. Drive LVGL timer handler. This is the only place LVGL APIs are
    //    invoked (spec §8.5 / DEC-03). Removing scrollables from the settings
    //    list (now a plain flex column) means this handler does not pull any
    //    gesture or scroll-chain work, so taps are not delayed by list drag
    //    motion.
    uint32_t next_call = lv_timer_handler();

    // 4. Report this iteration so the frame-time metrics on the diagnostics screen
    //    are measured numbers rather than placeholders (GATE-03 / TC-PERF-01).
    display_hal_record_ui_frame_ms(millis() - frame_start_ms);
    return next_call;
}

void UiManager::cycleTab() {
    switch (m_current_screen) {
        case UI_SCREEN_MEDIA:    navigateTo(UI_SCREEN_NAV); break;
        case UI_SCREEN_NAV:      navigateTo(UI_SCREEN_SETTINGS); break;
        case UI_SCREEN_SETTINGS: navigateTo(UI_SCREEN_MEDIA); break;
        // Splash, pairing, calibration, safemode and diagnostics are not part of
        // the cycle; a press leaves them for the media screen.
        default:                 navigateTo(UI_SCREEN_MEDIA); break;
    }
}

void UiManager::refreshSafetyState(uint32_t now_ms) {
    if (now_ms - m_last_state_sync_ms < 100) return;
    m_last_state_sync_ms = now_ms;

    // The touch fault is measured by the sampler, not derived from timeouts, so
    // the bar only has to mirror it. setTouchFault is a no-op when unchanged.
    m_status_bar.setTouchFault(touch_hal_is_faulted());

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
        // The nav screen owns the offline presentation: a lost link must not
        // leave the last turn cue or a frozen speedometer on glass.
        m_screen_nav.setLinkState(connected, bonded);
    }

    // Drive RGB LED to reflect HUD status and touch feedback
    int16_t tx = 0, ty = 0;
    const bool touching = touch_hal_get_point(&tx, &ty);
    if (touching) {
        board_set_led_rgb(true, true, true); // Flash white while touching
#if !defined(DASHCUE_NO_PAIRING)
    } else if (m_current_screen == UI_SCREEN_PAIRING) {
        board_set_led_rgb(true, true, false); // Amber during pairing window
#endif
    } else if (connected) {
        if (phone_stale) {
            // Amber blink: heartbeats missing, link stale.
            board_set_led_rgb((now_ms % 1000) < 500, true, false);
        } else if (!media_stale) {
            board_set_led_rgb(false, true, false); // Green while media is fresh/playing
        } else {
            board_set_led_rgb(false, false, true); // Blue when connected idle
        }
    } else if (m_bonded) {
        // Red blink: bonded phone lost (link loss).
        board_set_led_rgb((now_ms % 1000) < 500, false, false);
    } else {
        board_set_led_rgb(false, false, false); // Off: never paired
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
        // Theme is intentionally not applied here: the cockpit is dark-only
        // (ui_theme), so an incoming TH_LIGHT/TH_AUTO changes nothing on glass.

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

#if !defined(DASHCUE_NO_PAIRING)
    if (m_current_screen == UI_SCREEN_PAIRING) {
        if (app_state_is_pairing_window_active()) {
            m_screen_pairing.setRemainingSeconds(app_state_get_pairing_remaining_s());
        } else {
            navigateTo(UI_SCREEN_MEDIA);
        }
    }
#endif
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

#if !defined(DASHCUE_NO_PAIRING)
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
#endif

            case IPC_EVT_BLE_STATE:
                m_status_bar.setBleStatus(msg.data.ble_state.is_connected, false);
                if (!msg.data.ble_state.is_connected) {
                    artwork_store_invalidate();
                }
                break;

            case IPC_EVT_ARTWORK_READY:
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
