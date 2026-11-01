// DashCue firmware configuration constants.
//
// Board identity, pins and display settings live here so hardware variants
// can be reviewed against one source of truth.
#pragma once

#include <stdint.h>

// ------------------------------------------------------------------ board id
// Reported in HELLO.hw_revision and DEVICE_STATUS. Bump when a board variant
// needs a different pin map or driver setting.
// Target board: ESP32-3248S035 (3.5" 480x320 ST7796 + XPT2046 resistive touch on shared VSPI).
#define DASHCUE_HW_REVISION "cyd-3248s035r-st7796"

// ----------------------------------------------------------------- device id
// The HUD's stable identity is derived at runtime from the ESP32 efuse MAC
// (see hal/device_id.cpp). No compile-time MAC is baked in.

// --------------------------------------------------------------------- pins
// ESP32-3248S035 (ESP32-WROOM-32, 4 MB flash, ST7796 3.5" 480x320 + XPT2046).
#define PIN_LCD_MISO 12
#define PIN_LCD_MOSI 13
#define PIN_LCD_SCLK 14
#define PIN_LCD_CS 15
#define PIN_LCD_DC 2
#define PIN_LCD_RST -1   // tied to the ESP32 EN/reset net on this board
#define PIN_LCD_BL 27    // backlight enable on 3.5" CYD (GPIO 27); PWM'd for brightness (spec §12.6)
#define PIN_TOUCH_CS 33
#define PIN_TOUCH_IRQ 36 // PENIRQ probe on GPIO 36

// Shared SPI bus: display and touch share SCLK 14, MOSI 13 and MISO 12,
// with separate chip-select lines.
#define PIN_TOUCH_SCLK 14
#define PIN_TOUCH_MOSI 13
#define PIN_TOUCH_MISO 12

// Keep touch-bus assignments explicit so the touch HAL follows the selected
// board wiring rather than assuming a particular SPI topology.
#define PIN_LED_R 4
#define PIN_LED_G 16
#define PIN_LED_B 17
#define PIN_LDR 34       // ambient light sensor (analog, input only)
#define PIN_SPEAKER 26   // reserved, unused in v1 (no voice prompts, §2.3)

// The board's BOOT button, active LOW, with the pull-up the strapping pin already
// has. It is the only input left when the touch overlay fails, so the UI uses it
// to step through the tabs (see docs/TOUCH_FAULT_RUNBOOK.md).
#define PIN_BUTTON_BOOT 0

// The backlight is driven through a transistor, so HIGH = lit. If a board
// revision inverts this, change it here and re-run the M1 screen test; a wrong
// value is one of the failure modes GATE-03 exists to catch.
#define LCD_BACKLIGHT_ON HIGH
#define LCD_BACKLIGHT_OFF LOW

// Backlight PWM (LEDC): 5 kHz avoids visible flicker and audible switching noise.
#define BL_PWM_CHANNEL 0
#define BL_PWM_FREQ_HZ 5000
#define BL_PWM_RESOLUTION_BITS 8

// ------------------------------------------------------------------- display
#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 320

// ------------------------------------------------------------------- palette
// Design tokens from spec §12.10, converted to RGB565 for the panel and LVGL.
// Contrast against bg (#0B1220) is verified by tools/contrast_check.py; the
// values here must match the table there exactly.
// These values are generated and verified, never hand-converted: an 8-bit to
// 5/6/5 rounding mistake is invisible in review and wrong on the panel.
// tools/contrast_check.py recomputes each one from the token above it and fails
// the build if the two disagree.
#define DASHCUE_ACCENT_RGB565 0x65BF    // #67B7FF
#define DASHCUE_BG_RGB565 0x0021        // #05070A
#define DASHCUE_TEXT_RGB565 0xF7BE      // #F2F5F7
#define DASHCUE_TEXT_DIM_RGB565 0xADB8  // #A9B4C2
#define DASHCUE_OK_RGB565 0x3693        // #34D399
#define DASHCUE_WARN_RGB565 0xFDAA      // #FFB454
#define DASHCUE_DANGER_RGB565 0xFB4D    // #FF6B6B
// LVGL partial-render buffers (DEC-03, §16 memory plan): two 480x10 RGB565
// strips = 19,200 bytes total (or single 9,600 byte strip), which keeps refresh DMA-friendly
// without exhausting internal RAM.
#define LVGL_BUF_LINES 10

// ------------------------------------------------------------------- timing
// Runtime timing budgets used by the UI, touch sampler and connection watchdog.
#define UI_FRAME_TARGET_MS 50        // §16: UI render >= 20 FPS during animation
#define UI_FRAME_HARD_LIMIT_MS 100   // §16: no frame may exceed 100 ms
#define TOUCH_SAMPLE_HZ 100          // §8.4: touch_task samples every 10 ms
#define HEARTBEAT_INTERVAL_MS 2000   // §9.6: heartbeat every 2 s
#define LINK_DEAD_MISSES 10          // 10 missed heartbeats = link dead (20 s)
#define STALE_MEDIA_MS 30000         // media stale at 30 s (automotive tolerance)
#define STALE_NAV_MS 20000           // nav stale at 20 s
#define STALE_PHONE_MS 20000         // phone stale at 20 s
#define STALE_ANNOUNCE_MS 20000      // HUD shows stale at <= 20 s
#define RECONNECT_TARGET_MS 15000    // §16: reconnect p95 <= 15 s
#define BOOT_TO_UI_TARGET_MS 3000    // §16: boot to usable UI <= 3 s

// ------------------------------------------------------------------- policy
// Free heap and largest contiguous block are separate health signals: total
// free memory predicts sustained capacity, while the largest block predicts
// whether a single BLE/display allocation can succeed. The A2DP profile has a
// lower steady-state free-heap budget than BLE-only; see HARDWARE_REPORT.md.
#define DASHCUE_HEAP_FLOOR_FREE_BYTES 32000u     // minimum steady-state free heap
#define DASHCUE_HEAP_FLOOR_LARGEST_BYTES 16384u  // minimum contiguous allocation block
// Media-screen accent crossfade guard: each animation frame repaints rounded
// borders, and a BORDER draw task allocates a transient mask per frame. When
// A2DP streaming has squeezed the largest free block below this line the
// fade is skipped and the accent snaps (ui_screen_media.cpp). Measured on the
// 2026-10-25 soak: every draw-task failure cluster lands on a fade frame.
#define DASHCUE_ANIM_MIN_LARGEST_BYTES 8192u
// Heap guard for artwork transfers: an 8,192 B image crosses BLE as a burst
// of variable-size host-allocations that fragments the pool exactly while
// A2DP streaming needs a ~1 KB contiguous block for Bluedroid's HCI ACL
// reassembler; when that allocation fails the precompiled stack NULL-writes
// (packet_fragmenter.c:175, symbolized in both 2026-10-26 soak crashes).
// Below this largest-block line, artwork transfers are deferred and resend
// requests are paused; the next snapshot retry resumes after memory recovers.
#define DASHCUE_HEAP_GUARD_BYTES 16384u
// A2DP music level (ESP32-A2DP scale 0..127). 100 = 78.7 % of full DAC swing,
// below the alert path's 90 %-of-full-scale reference used for the 8 Ω 0.5 W
// speaker ceiling (AUDIO_SAFE_PEAK_COUNTS), so it stays inside the analysed
// envelope while giving ~1.9 dB over the old base of 80. Duck steps in
// main.cpp are the same S-curve scaled to this base (100 -> 25).
#define DASHCUE_A2DP_BASE_VOLUME 100
#define DASHCUE_A2DP_DUCK_VOLUME 25

// ----------------------------------------------------------------- build mode
// Diagnostic builds may include the USB-serial command console; production
// builds leave it disabled to prevent access to engineering-only commands.
#define DASHCUE_DIAG_SERIAL_COMMAND_CONSOLE 0

// Test doubles belong only in host test builds; reject accidental inclusion in
// firmware sources so simulated behavior cannot ship in the device image.
#ifdef DASHCUE_TEST_DOUBLES
#error "test doubles must never be compiled into firmware release sources (DEC-26)"
#endif
