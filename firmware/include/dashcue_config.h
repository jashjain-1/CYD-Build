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
// Measured board: ESP32 rev 3, 4 MB flash @ 40 MHz, no PSRAM,
// MAC D4:E9:F4:6F:F0:88, display works with the ILI9341 driver configuration
// but does not answer RDDID (returns 0x0000), touch on its own SPI bus.
#define DASHCUE_HW_REVISION "cyd-2432s028r-r1"

// ----------------------------------------------------------------- device id
// The HUD's stable identity is derived at runtime from the ESP32 efuse MAC
// (see hal/device_id.cpp). No compile-time MAC is baked in.

// --------------------------------------------------------------------- pins
// ESP32-2432S028R (ESP32-WROOM-32, 4 MB flash, ILI9341 2.8" 320x240 + XPT2046).
// These are the values measured and confirmed by the M1 hardware probe.
#define PIN_LCD_MISO 12
#define PIN_LCD_MOSI 13
#define PIN_LCD_SCLK 14
#define PIN_LCD_CS 15
#define PIN_LCD_DC 2
#define PIN_LCD_RST -1   // tied to the ESP32 EN/reset net on this board
#define PIN_LCD_BL 21    // backlight enable; PWM'd for brightness (spec §12.6)
#define PIN_TOUCH_CS 33
#define PIN_TOUCH_IRQ -1 // not wired on this board: polled, never interrupt-driven

// MEASURED BY GATE-02 on 2026-10-16 (see docs/HARDWARE_REPORT.md).
// §5.2 offered two hypotheses for the touch wiring. The scan in
// tools/hw_probe/src/main.cpp found the XPT2046 answering on its OWN bus and
// answering nothing at all on the display bus (every CS candidate read 0):
//   separate bus -> SCK 25, MOSI 32, MISO 39, CS 33   [this board]
//   shared bus   -> SCK 14, MOSI 13, MISO 12, CS 33   [rejected: no response]
// So the touch controller MUST NOT be driven from the display bus on this
// revision. These constants are the measured truth, not the hypothesis.
#define PIN_TOUCH_SCLK 25
#define PIN_TOUCH_MOSI 32
#define PIN_TOUCH_MISO 39

// The display bus pins (HSPI/VSPI, driven by TFT_eSPI) are PIN_LCD_* above.
// A board revision that shares one bus for both would set PIN_TOUCH_SCLK to
// PIN_LCD_SCLK and re-run GATE-02; the HAL must read these constants rather
// than assume a single bus.
#define PIN_LED_R 4
#define PIN_LED_G 16
#define PIN_LED_B 17
#define PIN_LDR 34       // ambient light sensor (analog, input only)
#define PIN_SPEAKER 26   // reserved, unused in v1 (no voice prompts, §2.3)

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
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240

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
// LVGL partial-render buffers (DEC-03, §16 memory plan): two 320x10 RGB565
// strips = 12,800 bytes total, which keeps the refresh DMA-friendly without
// exhausting internal RAM.
#define LVGL_BUF_LINES 10

// ------------------------------------------------------------------- timing
// Budgets from spec §16, enforced/monitored in code rather than aspirational.
#define UI_FRAME_TARGET_MS 50        // §16: UI render >= 20 FPS during animation
#define UI_FRAME_HARD_LIMIT_MS 100   // §16: no frame may exceed 100 ms
#define TOUCH_SAMPLE_HZ 100          // §8.4: touch_task samples every 10 ms
#define HEARTBEAT_INTERVAL_MS 2000   // §9.6: heartbeat every 2 s
#define LINK_DEAD_MISSES 6           // 6 missed heartbeats = link dead
#define STALE_MEDIA_MS 12000         // media stale at 12 s (automotive tolerance)
#define STALE_NAV_MS 10000           // nav stale at 10 s
#define STALE_PHONE_MS 12000         // phone stale at 12 s
#define STALE_ANNOUNCE_MS 12000      // HUD shows stale at <= 12 s
#define RECONNECT_TARGET_MS 15000    // §16: reconnect p95 <= 15 s
#define BOOT_TO_UI_TARGET_MS 3000    // §16: boot to usable UI <= 3 s

// ------------------------------------------------------------------- policy
// §16 absolute floor for steady state. Telemetry prints a WARN line below this
// instead of letting a slow leak surface as a mystery crash in the field.
#define DASHCUE_HEAP_FLOOR_BYTES 40960u

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
