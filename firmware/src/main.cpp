#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/task.h>
#include <string.h>

#include "dashcue_config.h"
#include "dashcue_version.h"
#include "board/board_hal.h"
#include "display/display_hal.h"
#include "touch/touch_hal.h"
#include "ui/ui_manager.h"
#include "state/app_state.h"
#include "state/ipc_queues.h"
#include "state/ipc_messages.h"
#include "transport/proto_task.h"
#include "transport/ble_config.h"
#include "transport/ble_peripheral.h"
#include "ui/artwork_store.h"

#if defined(DASHCUE_ENABLE_A2DP)
// Measurement build only (env:probe_a2dp). The sink is started before the BLE
// peripheral because A2DP needs the controller in BTDM (classic + BLE), and the
// controller must be configured before the first host init.
#include <BluetoothA2DPSink.h>
static BluetoothA2DPSink s_a2dp_sink;
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
    // 3a. A2DP sink (measurement build): classic BT must be up before the BLE
    //     host initialises, otherwise the controller is already committed to
    //     BLE-only and A2DP has no classic path to attach to.
    Serial.println("STEP a2dp"); Serial.flush();
    s_a2dp_sink.set_volume(60);
    s_a2dp_sink.start("DashCue Audio");
    const uint32_t a2dp_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    Serial.printf("A2DP: sink started, heap free=%u largest=%u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)a2dp_largest);
    Serial.flush();
    // Fallback, not a gate: if the sink left the heap below the audio floor the
    // HUD keeps working as a display and controller (BLE) rather than risking an
    // allocation failure deep inside the audio path later.
    if (a2dp_largest < 24576u) {
        Serial.println("A2DP: headroom below floor, stopping sink and keeping BLE-only");
        s_a2dp_sink.end();
    }
#endif

    // 3. BLE peripheral (Bluedroid, DEC-05 fallback). Deliberately before the UI is
    //    built: the BT controller + host need large contiguous heap blocks, and a
    //    heap already fragmented by ~48 KB of LVGL screen objects could not serve
    //    them (esp_bluedroid_init rc=-1, which the phone sees as "no HUD").
    Serial.println("STEP ble"); Serial.flush();
    if (ble_peripheral_init()) {
        Serial.println("BLE peripheral initialised (advertising per state)");
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
}

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
        if (largest < DASHCUE_HEAP_FLOOR_BYTES) {
            Serial.printf("WARN heap_floor largest_block=%u below %u\n",
                          (unsigned)largest, (unsigned)DASHCUE_HEAP_FLOOR_BYTES);
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
