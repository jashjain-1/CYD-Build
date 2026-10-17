# Hardware report

One board is available and was measured on **2026-10-16**. Everything in this
document is an observation from that board, not a specification claim or a
vendor-datasheet assumption.

## 1. Identified board

| Property | Measured value | How |
|---|---|---|
| Board | ESP32-2432S028R "Cheap Yellow Display" | visual identification by the operator |
| MCU | ESP32 (dual core), revision 3 | `esp_chip_info` |
| CPU | 240 MHz | `getCpuFrequencyMhz` |
| Flash | 4 194 304 bytes (4 MB), 40 MHz | `esp_flash_get_size`, `ESP.getFlashChipSpeed` |
| PSRAM | none (`PSRAM_BYTES=0`) | `ESP.getPsramSize` |
| Heap after boot | 318 472 free, 110 580 largest block | `ESP.getFreeHeap`, `heap_caps_get_largest_free_block` |
| MAC (device id source) | `D4:E9:F4:6F:F0:88` | `esp_read_mac(ESP_MAC_WIFI_STA)` |
| Reset reason | 1 (power-on) | `esp_reset_reason` |
| Serial | CH340 USB-serial on COM4 | `pio device list` |
| Display | 320 × 240, works with the TFT_eSPI ILI9341 configuration | screenshot + timings |
| Display controller ID | **UNKNOWN** — RDDID (0x04) returns `0x0000` | `tft.readcommand8(0x04, n)` |
| Touch | XPT2046 on its **own** SPI bus: SCK 25 / MOSI 32 / MISO 39 / CS 33 | bus/CS scan, §3 below |
| Ambient light sensor | present on GPIO 34 | analog read |

Evidence: [docs/evidence/20261016/TC-HW-01/](evidence/20261016/TC-HW-01/) and
[docs/evidence/20261016/TC-HW-04/](evidence/20261016/TC-HW-04/).

## 2. Display measurements

| Measurement | Value | Against the §16 budget |
|---|---|---|
| Full-screen fill (320×240) | 31 239 µs | — |
| 100×100 fill | 4 086 µs | — |
| 320×40 RGB565 strip blit (one partial-render flush) | 5 619 µs | — |
| Single-buffered full-frame estimate | ~29 FPS | target is ≥ 20 FPS during animation → **PASS** |

The FPS figure is an *estimate* derived from six strip blits (240 / 40 lines). It
is an upper bound for a full-frame redraw and is not a substitute for the LVGL
frame-time measurement that GATE-03/TC-PERF-01 requires.

**Controller identity is unresolved.** RDDID returns `0x0000`, which clone
panels commonly do. The ILI9341 configuration drives this panel correctly, so the
product uses it, but the report does not claim the panel *is* an ILI9341 — that
claim would be unverified (Rule 15/16). A revision whose panel needs ST7789 is
covered by the `cyd_st7789` environment.

## 3. The touch bus question (§5.2) — resolved by measurement

§5.2 listed two hypotheses: touch sharing the display bus, or touch on its own
bus. The probe scanned both pin sets against nine chip-select candidates and
identified liveness by reading variance, because a single reading cannot
distinguish a real controller from a floating MISO line:

| Bus | Pins | Result |
|---|---|---|
| shared (display bus) | SCK 14 / MISO 12 / MOSI 13, CS 33 | **no response on any CS** (all reads `0`) |
| separate | SCK 25 / MISO 39 / MOSI 32, CS 33 | **alive**, X channel drifting `[3666..3848]` |

Full scan output: [docs/evidence/20261016/TC-HW-01/hw_probe_scan.log](evidence/20261016/TC-HW-01/hw_probe_scan.log).

**Consequence:** `firmware/include/dashcue_config.h` carries the measured pin map
(`PIN_TOUCH_SCLK 25`, `PIN_TOUCH_MOSI 32`, `PIN_TOUCH_MISO 39`), and the firmware
must never read the touch controller from the display bus on this revision. This
also means GATE-04's calibration constants are tied to a specific board revision,
which is why the revision string is compiled in and reported over the protocol.

## 4. Touch measurements

| Measurement | Value |
|---|---|
| Controller | XPT2046, separate SPI bus, CS 33 (no IRQ line: polled) |
| Polling rate observed | 4 160 Hz (target is 100 Hz sampling in §8.4) |
| Raw range observed during one touch | x `[2..2319]`, y `[1347..3620]` |
| Saturated-read failure mode | a floating bus returns `0xFFFF` → x=y=8191, z=4095, which **passes** a naive pressure test |

That last row is recorded deliberately: the first version of the probe reported
"both buses responded" because saturated reads look like a touch. Firmware must
therefore treat `z ≥ 4090` or out-of-range `x`/`y` as *no signal*, never as a
press (Rule 10 in the specification is about calibration safety, and this is the
same class of failure).

The raw range above covers only where the operator touched, so it is **not** the
panel's full raw range. The 5-point calibration, residual error and physical
pixel pitch that GATE-04/TC-HW-03 require are still **NOT RUN**.

## 5. Orientation

The operator confirmed the landscape orientation used by the probe (TFT_eSPI
`setRotation(1)`) is **correct** for this board on 2026-10-16. No 180° correction
is needed for the default mount. A rotated-180 option remains a user setting per
§12.6.

## 6. What is still unverified on this board

- Which controller the panel actually uses (RDDID unimplemented).
- The full touch raw range and the 5-point calibration residual (GATE-04).
- Touch accuracy across the panel surface, and the physical pixel pitch in mm
  (needs a ruler and a stylus).
- Backlight brightness range, PWM flicker and persistence (TC-HW-06).
- Everything below the display: BLE, protocol, UI, the optional Wi-Fi channel.
