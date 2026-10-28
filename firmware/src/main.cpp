#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/task.h>
#include <string.h>

#include "dashcue_config.h"
#include "dashcue_version.h"
#include "board/board_hal.h"
#include "display/display_hal.h"
#include "touch/touch_hal.h"
#include "audio/audio_hal.h"
#include "ui/ui_manager.h"
#include "state/app_state.h"
#include "state/ipc_queues.h"
#include "state/ipc_messages.h"
#include "transport/proto_task.h"
#include "transport/ble_config.h"
#include "transport/ble_peripheral.h"
#include "ui/artwork_store.h"

#if defined(DASHCUE_ENABLE_A2DP)
#include "transport/dashcue_a2dp_sink.h"
#include <esp_gap_bt_api.h>
#include <driver/i2s.h>
#include <driver/dac.h>
#include <driver/gpio.h>
#include <esp_timer.h>

static DashCueA2DPSink s_a2dp_sink;

// 16-step S-curve transition: round(100.0 - 75.0 * 0.5 * (1.0 - cos(pi * i / 16.0)))
// Smoothly ducks from DASHCUE_A2DP_BASE_VOLUME (100) to DASHCUE_A2DP_DUCK_VOLUME
// (25) in 5 ms steps (total 80 ms) - same shape as the original 80 -> 20 curve,
// rescaled when the base level rose for the 8 Ω 0.5 W speaker.
static const uint8_t kDuckSteps[17] = {
    100, 99, 97, 94, 89, 83, 77, 70, 63, 55, 48, 42, 36, 31, 28, 26, 25
};

static esp_timer_handle_t s_duck_timer = nullptr;
static volatile bool s_duck_target = false; // true = ducking to 20%, false = unducking to 80%
static volatile int s_duck_step = 0;        // 0 = 80%, 16 = 20%

static void IRAM_ATTR duck_timer_cb(void *arg) {
    (void)arg;
    if (s_duck_target) {
        if (s_duck_step < 16) {
            s_duck_step++;
            s_a2dp_sink.set_volume(kDuckSteps[s_duck_step]);
        } else {
            esp_timer_stop(s_duck_timer);
        }
    } else {
        if (s_duck_step > 0) {
            s_duck_step--;
            s_a2dp_sink.set_volume(kDuckSteps[s_duck_step]);
        } else {
            esp_timer_stop(s_duck_timer);
        }
    }
}

static void a2dp_duck_callback(bool duck) {
    s_duck_target = duck;
    if (s_duck_timer != nullptr) {
        esp_timer_stop(s_duck_timer);
        esp_timer_start_periodic(s_duck_timer, 5000); // 5 ms step
    } else {
        s_a2dp_sink.set_volume(duck ? DASHCUE_A2DP_DUCK_VOLUME : DASHCUE_A2DP_BASE_VOLUME);
    }
}

extern "C" void dashcue_a2dp_connect_peer(const uint8_t *bda) {
    (void)bda;
    esp_a2d_connection_state_t state = s_a2dp_sink.get_connection_state();
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED ||
        state == ESP_A2D_CONNECTION_STATE_CONNECTING) {
        return;
    }
    Serial.println("A2DP: BLE central active, auto-connecting A2DP profile...");

    // 1. Prioritize authentic Classic BT BD_ADDR from esp_bt_gap bond list (avoids BLE RPA failures)
    int num_classic = esp_bt_gap_get_bond_device_num();
    if (num_classic > 0) {
        esp_bd_addr_t *classic_list = (esp_bd_addr_t *)malloc(sizeof(esp_bd_addr_t) * num_classic);
        if (classic_list != nullptr) {
            int query_num = num_classic;
            esp_bt_gap_get_bond_device_list(&query_num, classic_list);
            const uint8_t *peer = nullptr;
            for (int i = 0; i < query_num; ++i) {
                if (app_state_is_valid_bt_addr(classic_list[i])) {
                    peer = classic_list[i];
                    break;
                }
            }
            if (peer != nullptr) {
                Serial.printf("A2DP: connecting to bonded Classic BT peer %02X:%02X:%02X:%02X:%02X:%02X\n",
                              peer[0], peer[1], peer[2], peer[3], peer[4], peer[5]);
                esp_bd_addr_t target;
                memcpy(target, peer, sizeof(target));
                s_a2dp_sink.connect_to(target);
                free(classic_list);
                return;
            }
            free(classic_list);
        }
    }

    // 2. Fall back to reconnect() (queries NVS last_bda preserved across reboots)
    if (s_a2dp_sink.has_last_connection()) {
        const uint8_t *stored = (const uint8_t *)s_a2dp_sink.get_last_peer_address();
        if (stored != nullptr && app_state_is_valid_bt_addr(stored)) {
            Serial.printf("A2DP: reconnecting to NVS stored Classic BT peer %02X:%02X:%02X:%02X:%02X:%02X\n",
                          stored[0], stored[1], stored[2], stored[3], stored[4], stored[5]);
            if (s_a2dp_sink.reconnect()) {
                return;
            }
        }
    }

    // 3. If no Classic BT bond exists yet, DO NOT connect to the BLE RPA!
    // Paging a BLE RPA over Classic BT causes timeout lockups and disables discoverability.
    // Instead, ensure Classic BT scan mode is actively Connectable & Discoverable so the
    // phone can find "DashCue-HUD" in Bluetooth settings and initiate pairing.
    Serial.println("A2DP: No bonded Classic BT peer yet; scan mode set to Connectable & Discoverable");
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
}

extern "C" void dashcue_a2dp_forget_peer(void) {
    s_a2dp_sink.force_clean_last_connection();
}

// Restore A2DP sink's NVS last-connection from stored NVS or the GAP bond list.
static void restore_a2dp_last_connection(void) {
    if (s_a2dp_sink.has_last_connection()) {
        const uint8_t *stored = (const uint8_t *)s_a2dp_sink.get_last_peer_address();
        if (stored != nullptr && app_state_is_valid_bt_addr(stored)) {
            Serial.printf("A2DP: Verified valid stored Classic BT peer in NVS: %02X:%02X:%02X:%02X:%02X:%02X\n",
                          stored[0], stored[1], stored[2], stored[3], stored[4], stored[5]);
            return; // Valid stored Classic BT connection in NVS, keep it!
        }
        Serial.println("A2DP: Stored peer address invalid, cleaning NVS connection");
        s_a2dp_sink.force_clean_last_connection();
    }

    int num_bonded = esp_bt_gap_get_bond_device_num();
    if (num_bonded <= 0) return;
    esp_bd_addr_t *bda_list = (esp_bd_addr_t *)malloc(sizeof(esp_bd_addr_t) * num_bonded);
    if (bda_list == nullptr) return;
    int query_cnt = num_bonded;
    esp_bt_gap_get_bond_device_list(&query_cnt, bda_list);
    const uint8_t *valid = nullptr;
    for (int i = 0; i < query_cnt && valid == nullptr; ++i) {
        if (app_state_is_valid_bt_addr(bda_list[i])) valid = bda_list[i];
    }
    if (valid != nullptr) {
        Serial.printf("A2DP: Restoring bonded Classic BT address to NVS: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      valid[0], valid[1], valid[2], valid[3], valid[4], valid[5]);
        esp_bd_addr_t chosen;
        memcpy(chosen, valid, sizeof(chosen));
        s_a2dp_sink.set_last_connection(chosen);
    }
    free(bda_list);
}

static void a2dp_audio_state_changed(esp_a2d_audio_state_t state, void *ptr) {
    (void)ptr;
    Serial.printf("A2DP: state changed -> %s\n",
                  state == ESP_A2D_AUDIO_STATE_STARTED ? "STARTED" :
                  state == ESP_A2D_AUDIO_STATE_STOPPED ? "STOPPED" : "REMOTE_SUSPEND");
    if (state == ESP_A2D_AUDIO_STATE_STARTED) {
        audio_hal_notify_a2dp_state(true);
        dac_output_enable(DAC_CHANNEL_2);
        i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
    } else {
        // When stopped or suspended, reset biquad filter state, notify Audio HAL and clamp pin 26 to 0V to kill amplifier idle hiss
        s_a2dp_sink.reset_filter();
        audio_hal_notify_a2dp_state(false);
        audio_hal_clamp_zero_volts();
    }
}
#else
extern "C" void dashcue_a2dp_connect_peer(const uint8_t *bda) { (void)bda; }
extern "C" void dashcue_a2dp_forget_peer(void) {}
#endif

static uint32_t boot_started_ms = 0;
static bool boot_failed = false;
static void fail_boot(const char *stage) {
    boot_failed = true;
    Serial.printf("BOOT FAILED stage=%s; UI startup halted. Check hardware and free heap.\n", stage);
    Serial.flush();
    // Also put it on the glass. A black panel is indistinguishable from a dead
    // board, and this path runs exactly when nobody has a serial console attached.
    // No-op if the display never came up, which is the one failure it cannot draw.
    if (display_hal_is_ready()) {
        display_hal_show_boot_error(stage);
    }
}

void setup() {
    boot_started_ms = millis();
    Serial.begin(115200);
    delay(100);

    Serial.println();
    Serial.printf("DashCue fw=%s hw=%s\n", DASHCUE_FW_VERSION_STRING, DASHCUE_HW_REVISION);

    // Read-only boot breadcrumbs: if a step hangs, the last line printed says which
    // one, which is the difference between a diagnosable field failure and a brick.
    // 1. Board HAL: GPIOs, PWM backlight, RTC crash tracking, efuse MAC
    Serial.println("STEP board"); Serial.flush();
    board_init();

    // 2. State & FreeRTOS IPC Queues
    Serial.println("STEP state"); Serial.flush();
    if (!ipc_queues_init() || !app_state_init()) {
        fail_boot("state_queues");
        return;
    }
    artwork_store_init();
    app_state_record_boot((uint32_t)board_get_reset_reason(), millis() / 1000u);

#if defined(DASHCUE_ENABLE_A2DP)
    // 2b. A2DP sink: start Classic BT A2DP music sink on built-in DAC (GPIO 26)
    Serial.println("STEP a2dp"); Serial.flush();
    // NOTE: the sink's NVS last-connection restore intentionally happens later
    // (after ble_peripheral_init): it corroborates against the BLE-bonded
    // identity, which is only readable once the BLE host is up.

    esp_timer_create_args_t duck_timer_args = {};
    duck_timer_args.callback = &duck_timer_cb;
    duck_timer_args.dispatch_method = ESP_TIMER_TASK;
    duck_timer_args.name = "a2dp_duck";
    esp_timer_create(&duck_timer_args, &s_duck_timer);

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_MSB,
        .intr_alloc_flags = 0,
        .dma_buf_count = 6,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };
    s_a2dp_sink.set_i2s_config(i2s_config);
    s_a2dp_sink.set_mono_downmix(true);
    s_a2dp_sink.set_volume(DASHCUE_A2DP_BASE_VOLUME);
    s_a2dp_sink.set_on_audio_state_changed(a2dp_audio_state_changed);
    s_a2dp_sink.start("DashCue-HUD");
    s_a2dp_sink.set_auto_reconnect(true, 1000);
    const uint32_t a2dp_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    Serial.printf("A2DP: sink started, heap free=%u largest=%u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)a2dp_largest);
    Serial.flush();
#endif

    // 3. BLE peripheral (Bluedroid, DEC-05 fallback). Deliberately before the UI is
    //    built: the BT controller + host need large contiguous heap blocks, and a
    //    heap already fragmented by ~48 KB of LVGL screen objects could not serve
    //    them (esp_bluedroid_init rc=-1, which the phone sees as "no HUD").
    Serial.println("STEP ble"); Serial.flush();
    if (ble_peripheral_init()) {
        Serial.println("BLE peripheral initialised (advertising per state)");
#if defined(DASHCUE_ENABLE_A2DP)
        // BLE identity is now readable: restore the A2DP sink's NVS last-
        // connection corroborated against the BLE-bonded phone (junk entries
        // volunteered by the GAP bond store are rejected here).
        restore_a2dp_last_connection();
#endif
    } else {
        Serial.println("BLE peripheral INIT FAILED - HUD will not be discoverable");
        fail_boot("ble");
        return;
    }
    Serial.flush();

    // 4. Display HAL: TFT_eSPI, LVGL 9 display, dual draw buffers, pointer indev
    Serial.println("STEP display"); Serial.flush();
    if (!display_hal_init()) {
        fail_boot("display");
        return;
    }

    // 4. Touch HAL: XPT2046 on dedicated HSPI, NVS calibration, 100 Hz Core 1 task
    Serial.println("STEP touch"); Serial.flush();
    touch_hal_setup();
    if (touch_hal_start_task()) {
        Serial.println("TOUCH task started (Core 1, 100 Hz)");
    } else {
        Serial.println("TOUCH: failed to start touch_task!");
        fail_boot("touch_task");
        return;
    }
    Serial.flush();

    // 4b. Onboard speaker (GPIO26 / DAC2): restores output mode, volume and EQ
    //     from NVS and starts the playback timer. Failure here only means a
    //     silent HUD, never a dead one, so it deliberately does not fail_boot.
    Serial.println("STEP audio"); Serial.flush();
    audio_hal_init();
#if defined(DASHCUE_ENABLE_A2DP)
    audio_hal_set_duck_callback(a2dp_duck_callback);
#endif
    Serial.flush();

    // 5. Protocol task on Core 0 (owns reassembly, dedupe, dispatch).
    // Started before UI so FreeRTOS task stacks and IPC queues allocate in contiguous
    // lower heap, preventing fragmentation with LVGL object allocations (§16: largest block >= 20 KB).
    Serial.println("STEP proto"); Serial.flush();
    if (!proto_task_start()) {
        fail_boot("proto_task");
        return;
    }

    // 6. UI Manager: status+tab bars + warm screens, ui_task on Core 1 priority 4
    Serial.println("STEP ui"); Serial.flush();
    if (!UiManager::instance().init()) {
        fail_boot("ui_task");
        return;
    }

    // 7. First-run pairing (spec §12.7): with no bonded phone the pairing screen is
    //    shown on first boot. That screen is only useful while the 120 s pairing
    //    window is advertising, so the window starts with it. Entry by explicit
    //    user action is still the only way in once a phone is bonded.
    //
    //    Sent through the IPC queue so the BLE owner (proto task) performs it,
    //    exactly like the HUD's Pair button, rather than setup() reaching into the
    //    peripheral directly.
    if (!app_state_has_bonded_phone()) {
        Serial.println("PAIR no bonded phone - requesting first-run pairing window (120 s)");
        UiToProtoMsg msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = IPC_CMD_PAIR_PHONE;
        msg.data.pair_phone.timeout_s = DASHCUE_PAIRING_WINDOW_MS / 1000u;
        ipc_send_ui_to_proto(&msg, 0);
    }

    uint32_t first_frame_ms = millis() - boot_started_ms;
    Serial.printf("BOOT boot_to_ui_ms=%u (target <= %u ms)\n",
                  (unsigned)first_frame_ms, (unsigned)BOOT_TO_UI_TARGET_MS);
    Serial.printf("HEAP after init free=%u min=%u largest=%u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMinFreeHeap(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (ESP.getFreeHeap() < DASHCUE_HEAP_FLOOR_FREE_BYTES) {
        Serial.printf("WARN heap_floor free=%u below %u at boot end\n",
                      (unsigned)ESP.getFreeHeap(), (unsigned)DASHCUE_HEAP_FLOOR_FREE_BYTES);
    }}

// Stack headroom for the three long-lived tasks. A stack size is only ever
// trimmed against a measured high-water mark, never a guess, so these lines are
// the evidence that makes a trim reviewable. ESP-IDF reports the mark in bytes
// (vanilla FreeRTOS reports words).
static void log_stack_watermarks(void) {
    // uxTaskGetSystemState is not compiled into the Arduino FreeRTOS build
    // (configUSE_TRACE_FACILITY=0), so the tasks are looked up by name instead of
    // widening a kernel config the precompiled core owns.
    static const char *const names[] = {"ui_task", "proto_task", "touch_task"};
    for (const char *name : names) {
        TaskHandle_t h = xTaskGetHandle(name);
        if (h) {
            Serial.printf("STACK %s high_water=%u B unused\n", name,
                          (unsigned)uxTaskGetStackHighWaterMark(h));
        }
    }
}

void loop() {
    if (boot_failed) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }
    // All UI, Touch, and BLE processing run on dedicated FreeRTOS tasks:
    // - ui_task on Core 1 (priority 4, <= 5 ms loop)
    // - touch_task on Core 1 (priority 5, 100 Hz)
    // - proto_task on Core 0 (priority 3, event-driven)
    //
    // Arduino loop performs steady-state telemetry reporting every 5s
    static uint32_t last_log = 0;
    static uint32_t last_stack_log = 0;
    uint32_t now = millis();

    if (now - last_stack_log >= 30000) {
        last_stack_log = now;
        log_stack_watermarks();
    }

    if (now - last_log >= 5000) {
        last_log = now;
        const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        const uint32_t free_heap = ESP.getFreeHeap();
        // §16 floors for free heap and for the largest block are distinct
        // (dashcue_config.h): warn per metric so a real fragmentation problem
        // cannot hide behind the intentional A2DP free-heap budget.
        if (free_heap < DASHCUE_HEAP_FLOOR_FREE_BYTES) {
            Serial.printf("WARN heap_floor free=%u below %u\n",
                          (unsigned)free_heap, (unsigned)DASHCUE_HEAP_FLOOR_FREE_BYTES);
        }
        if (largest < DASHCUE_HEAP_FLOOR_LARGEST_BYTES) {
            Serial.printf("WARN heap_floor largest_block=%u below %u\n",
                          (unsigned)largest, (unsigned)DASHCUE_HEAP_FLOOR_LARGEST_BYTES);
        }
        // heap_free/largest_block are the §16 budget; px_per_s and fps are measured
        // by the display HAL from the pixels the flush callback actually pushed to
        // the panel, so "the UI renders" is a number here and not an assumption.
        Serial.printf("STATS uptime_s=%u heap_free=%u heap_min=%u largest_block=%u "
                      "ui_screen=%d render_px_per_s=%u fps_equiv=%u frame_avg_ms=%u frame_max_ms=%u\n",
                      (unsigned)(now / 1000), (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getMinFreeHeap(),
                      (unsigned)largest,
                      (int)UiManager::instance().getCurrentScreen(),
                      (unsigned)display_hal_get_render_px_per_s(),
                      (unsigned)display_hal_get_fps(),
                      (unsigned)display_hal_get_avg_frame_ms(),
                      (unsigned)display_hal_get_max_frame_ms());
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
}
