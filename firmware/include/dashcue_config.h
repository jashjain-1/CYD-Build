// DashCue firmware configuration constants.
//
// Everything that encodes a *fact about the hardware* lives here so a board
// revision that differs can be handled by varying one file (GATE-02 measures
// the real board and confirms these values before M1 is closed).
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

// Shared VSPI bus on 3.5" CYD (ESP32-3248S035):
// Display and touch share SCLK 14, MOSI 13, MISO 12 with dedicated CS lines (LCD 15, Touch 33).
#define PIN_TOUCH_SCLK 14
#define PIN_TOUCH_MOSI 13
#define PIN_TOUCH_MISO 12

// The display bus pins (HSPI/VSPI, driven by TFT_eSPI) are PIN_LCD_* above.
// A board revision that shares one bus for both would set PIN_TOUCH_SCLK to
// PIN_LCD_SCLK and re-run GATE-02; the HAL must read these constants rather
// than assume a single bus.
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

// Backlight PWM (LEDC). 5 kHz is well above the visible flicker threshold and
// keeps the transistor out of audible range.
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
// Budgets from spec §16, enforced/monitored in code rather than aspirational.
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
// §16 absolute floors for steady state: free heap and the largest contiguous
// free block are two different requirements, so they get two constants. The
// single DASHCUE_HEAP_FLOOR_BYTES value used to be applied to BOTH metrics,
// which printed a permanent(largest-block) WARN on the A2DP build even where
// §16 said the build was healthy; telemetry now reports each metric against
// its own floor. The A2DP build runs intentionally below the 40 KB free-heap
// floor (its steady-state budget is measured in docs/HARDWARE_REPORT.md §7.2);
// that gap is documented policy, not a leak signal, so the free-heap floor
// reflects what a real A2DP + BLE product build holds while the largest-block
// floor stays at the §16 line a fragmented heap actually fails on.
#define DASHCUE_HEAP_FLOOR_FREE_BYTES 32000u     // §16: min free heap at steady state (idle measured 38,3xx B)
#define DASHCUE_HEAP_FLOOR_LARGEST_BYTES 16384u  // idle measured 17,3xx B; == artwork-acceptance line; streaming sits intentionally below (2026-10-26 soak: 5,876 B)
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
// Below this largest-block line the sink DEFERS accepting artwork transfers
// (proto_task.cpp) and skips sending resend requests; the self-healing
// snapshot-request retry picks the transfer up once the heap recovers.
#define DASHCUE_HEAP_GUARD_BYTES 16384u
// A2DP music level (ESP32-A2DP scale 0..127). 100 = 78.7 % of full DAC swing,
// below the alert path's 90 %-of-full-scale reference used for the 8 Ω 0.5 W
// speaker ceiling (AUDIO_SAFE_PEAK_COUNTS), so it stays inside the analysed
// envelope while giving ~1.9 dB over the old base of 80. Duck steps in
// main.cpp are the same S-curve scaled to this base (100 -> 25).
#define DASHCUE_A2DP_BASE_VOLUME 100
#define DASHCUE_A2DP_DUCK_VOLUME 25

// ----------------------------------------------------------------- build mode
// DEC-27: the diagnostic build adds a USB-serial command console. Release
// builds MUST NOT define this. tools/check_release_scan.py greps the release
// artefact for the console's symbols to prove it is absent.
#define DASHCUE_DIAG_SERIAL_COMMAND_CONSOLE 0

// DEC-26: test doubles are compiled in test source sets only. This macro exists
// so release code can assert at compile time that no fake path leaked in.
#ifdef DASHCUE_TEST_DOUBLES
#error "test doubles must never be compiled into firmware release sources (DEC-26)"
#endif
