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
| Display controller ID | **UNKNOWN**, RDDID (0x04) returns `0x0000` | `tft.readcommand8(0x04, n)` |
| Touch | XPT2046 on its **own** SPI bus: SCK 25 / MOSI 32 / MISO 39 / CS 33 | bus/CS scan, §3 below |
| Ambient light sensor | present on GPIO 34 | analog read |

Evidence: [docs/evidence/20261016/TC-HW-01/](evidence/20261016/TC-HW-01/) and
[docs/evidence/20261016/TC-HW-04/](evidence/20261016/TC-HW-04/).

## 2. Display measurements

| Measurement | Value | Against the §16 budget |
|---|---|---|
| Full-screen fill (320×240) | 31 239 µs |, |
| 100×100 fill | 4 086 µs |, |
| 320×40 RGB565 strip blit (one partial-render flush) | 5 619 µs |, |
| Single-buffered full-frame estimate | ~29 FPS | target is ≥ 20 FPS during animation → **PASS** |

The FPS figure is an *estimate* derived from six strip blits (240 / 40 lines). It
is an upper bound for a full-frame redraw and is not a substitute for the LVGL
frame-time measurement that GATE-03/TC-PERF-01 requires.

**Controller identity is unresolved.** RDDID returns `0x0000`, which clone
panels commonly do. The ILI9341 configuration drives this panel correctly, so the
product uses it, but the report does not claim the panel *is* an ILI9341, that
claim would be unverified (Rule 15/16). A revision whose panel needs ST7789 is
covered by the `cyd_st7789` environment.

## 3. The touch bus question (§5.2), resolved by measurement

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

## 7. Bluetooth coexistence: what classic-BT costs (measured 2026-10-20)

An A2DP sink was under consideration, so what it would cost in RAM was measured
instead of estimated. Flash is **not** the constraint: the release binary is
1,685,856 B inside a 3 MB app partition (1,426 KB free), and the A2DP code is
*already linked into that binary*: the Bluedroid library is precompiled with
classic BT, and `nm` finds 84 `a2dp` symbols and 51 `sbc`/`btc_a2dp` symbols in
the shipping ELF. Deleting the 960 KB LittleFS partition would not free one byte
of RAM and would not add a byte of audio capability.

The constraint is SRAM, and the product deliberately releases the classic-BT
controller region (DECISIONS D-10). `env:probe_btdm` measures the other choice by
keeping that region and starting the controller in BTDM:

| Path | Heap after controller enable | Δ from 199,252 | STATS at 5 s, free | Largest block |
|---|---|---|---|---|
| Product, classic released, BLE-only | 170,980 | −28,272 | 67,448 | 45,044 |
| `probe_btdm`, classic kept, BTDM | 156,000 | −43,252 | 52,440 | 29,684 |
| **Cost of coexistence** | | **−14,980** | **−15,008** | **−15,360** |

Boot time was unaffected: `boot_to_ui_ms=1,409` (probe) vs `1,412` (release),
both inside the 3,000 ms budget.

This is a **lower bound**, and the report says so deliberately: the probe starts
no classic host and no A2DP/AVRCP profile, so Bluedroid's classic host, the SBC
decoder and the I2S DMA buffers are all absent from that figure. Those are what
must be measured before an A2DP sink can be called feasible on this 320 KB part.
What the numbers do establish is that dual-mode Bluetooth alone leaves 29.7 KB of
*contiguous* heap, below the 32 KB an A2DP ring buffer alone would want.

Evidence: [docs/evidence/20261020/bt_coex_probe/btdm_boot.log](evidence/20261020/bt_coex_probe/btdm_boot.log)
(probe) and [release_restore.log](evidence/20261020/bt_coex_probe/release_restore.log)
(the product build re-flashed afterwards, calibration blob still valid).
Reproduce with `pio run -e probe_btdm -t upload --upload-port COM4`.

**Hardware note for any upgrade:** ESP32-S3 supports Bluetooth LE only and has no
classic BT or LE Audio, so an S3-based CYD variant (e.g. ESP32-8048S050) can never
be an A2DP sink. A2DP pins this product to the original ESP32 family.

### 7.1 What the compression actually bought (2026-10-20)

Four changes, none of them visible to a user:

1. The Device Information Service and the alternate-UUID service were deleted
   (6,232 B of heap; the app declares DIS UUIDs but never reads them and has no
   constant for the ALT service).
2. The second draw buffer was removed (6,400 B of static DRAM; a strip flush is
   about 1.6 ms against a 16 ms frame period, so the overlap bought nothing
   measurable).
3. Task stacks were resized from measured high-water marks (9,216 B): ui
   12,288 to 8,192, proto 10,240 to 7,168, touch 4,096 to 2,048.
4. The payload cap was cut from 4,096 to 2,048 B (4,096 B of static reassembly
   buffers). It is advertised in `HELLO_ACK`, and the phone clamps its send size
   to `min(ack.maxMsgSize, its own max)`.

| Build | Heap at 5 s | Largest block | Static RAM |
|---|---|---|---|
| Before any work | 67,448 | 45,044 | 66,572 |
| After changes 1 to 3 | 92,588 | 69,620 | 60,140 |
| Current product, changes 1 to 4 | **96,548** | **73,716** | **56,044** |
| Current product + BTDM (measured before change 4) | 77,620 | 55,284 | 60,140 |
| Current product + BTDM + real A2DP sink | 55,772 | 32,756 | 60,880 |

Heap figures vary by about 100 B between boots. The A2DP row is a measurement
build, not a product configuration.

Measured stack headroom after resizing: ui 3,920 B, proto 3,564 B, touch 1,460 B
free, roughly 1.9x, 2.0x and 3.5x the observed peak. The `STACK` lines in the
periodic report are what a future trim must be justified against.

### 7.2 A2DP sink probe (env:probe_a2dp)

The ESP32-A2DP sink (pinned v1.8.2) starts on this board **with BLE kept alive**:
boot log shows `A2DP: sink started`, then `BLE peripheral initialised`, UI ready in
**1,378 ms**, calibration blob still valid, and the panel rendering. Steady state
keeps 55,772 B free with a 32,756 B contiguous block: 40.8 KB less free than the
audio-free build, and just above the 32 KB an A2DP ring buffer alone would want.
That margin is thin, which is why the build carries a floor check that stops the
sink instead of letting a later allocation fail inside the audio path.

| Comparison | Bytes |
|---|---|
| A2DP sink on top of BTDM (both measured before change 4) | 20,984 |
| Current product against the audio build (5 s heap) | 40,776 |

The 40,776 B figure is what audio costs against today's product; it is not the
same as the BTDM delta above because the two rows were measured on different
revisions of the payload cap.

**What this does not prove:** no audio was played. The probe deliberately leaves the
I2S pins at library defaults rather than the MAX98357A map (BCLK 22 / LRC 26 /
DIN 27), no phone was connected, and stream-time peak (SBC decode + ring buffer +
A2DP task stack) is unmeasured. It establishes that the RAM envelope exists; it does
not establish that sound comes out. Evidence:
[after_cuts.log](evidence/20261020/heap_compression/after_cuts.log),
[after_stack_trim.log](evidence/20261020/heap_compression/after_stack_trim.log),
[btdm_after_cuts.log](evidence/20261020/heap_compression/btdm_after_cuts.log),
[a2dp_probe.log](evidence/20261020/heap_compression/a2dp_probe.log).
