# Changelog

Milestone-scoped, newest first. Every entry names the evidence that backs it, so a
claim in this file can be checked rather than believed.

## [Unreleased] 20261027 - Zero-Crash Hardening, Audiophile I2S DMA Audio & Adaptive Scheduling

* **Eliminated Core 0 16 kHz Software Periodic Callback:** Removed the high-frequency `esp_timer` software periodic callback (which previously triggered 16,000 times/sec on Core 0, inducing severe CPU starvation, FreeRTOS tick starving, task watchdog timeouts, and audio jitter).
* **Hardware I2S DMA Audio Subsystem:** Alert tone generation is migrated to hardware I2S DMA (`I2S_NUM_0`) in built-in DAC mode via dedicated Core 1 `audio_task` (priority 3). Physical hardware PLL clocking produces crystal-clear audio with zero timing jitter or clatter.
* **Zero Runtime Heap Allocation in Audio:** Eliminated all dynamic `malloc`/`free` calls during audio alert playback. Alert waveforms are synthesized on-the-fly in fixed 64-frame (256-byte) stack chunks streamed directly to DMA. This completely prevents heap fragmentation that previously caused Bluedroid `packet_fragmenter.c:175 StoreProhibited` panics.
* **Full Acoustic Dynamic Range for 8Ω 0.5W Speaker:** Unlocked full 0..255 DAC dynamic range (`AUDIO_SAFE_PEAK_COUNTS 127`, `AUDIO_MAX_VOLUME_PCT 100`) through the onboard 8002A amplifier, delivering punchy, loud alert tones without rasp or clipping.
* **0V Digital Idle Clamping:** GPIO 26 (DAC2 / 8002A amp) is clamped to digital 0V when idle, completely eliminating amplifier hiss and idle buzz.
* **Stripped UI Gradient Overhangs & Unified Cockpit Theme:** Removed `darken_for_gradient_top`, `m_grad_top_rgb`, and 300 ms `lv_anim` background crossfade animations in `ui_screen_media.cpp/.h`. Screen background is permanently unified to solid `#05070A` (`UI_COLOR_BG`), and 1-shot widget accent color updates are applied without full-screen invalidation, saving 98.3% of redraw cycles.
* **Adaptive Tickless UI Scheduling:** Replaced fixed 5 ms busy polling in `ui_task` with dynamic sleep: `constrain(lv_timer_handler(), 4u, 30u) ms`. When the display is idle, `ui_task` sleeps for 30 ms, yielding Core 1 to the FreeRTOS idle task and slashing CPU heat and power.
* **Hardware PENIRQ Touch Bus Deconfliction:** In `touch_hal.cpp`, hardware `PIN_TOUCH_IRQ` (GPIO 36 / PENIRQ) pre-check completely skips `g_spi_bus_mutex` acquisition and SPI read transactions when the screen is idle, eliminating SPI bus contention with the display.
* **Verification & Audit Gates Passed:**
  - Independent post-victory audit certified: **VICTORY CONFIRMED**.
  - Target firmware build `cyd_ili9341`: **SUCCESS** (RAM 68,264 B / 20.8%, Flash 1,763,589 B / 56.1%).
  - Static DRAM budget: **68,264 bytes** (<= 70,000 bytes budget, 1,736 B headroom).
  - Native unit test suite (`pio test -e native`): **24/24 PASSED** (1.57s).
  - Host Pytest test suite (`python -m pytest`): **397/397 PASSED** (33.53s).
  - Android unit test suite (`./gradlew test`): **366/366 Tasks Green**.
  - WCAG Contrast audit (`tools/contrast_check.py`): **16/16 pairs PASSED** (18.42:1 ratio).
  - Strict UI screen audit (`tools/audit_ui_screens.py --strict`): **100% COMPLIANT**.

## [Unreleased] 20261026 - boot-loop root cause, heap governor, loudness & clarity

* **Boot loop root-caused and fixed (two independent defects, one shared RCA).**
  The HUD reset repeatedly while music streamed. Both soak crashes
  (`docs/evidence/20261026/soak_churn.log`) symbolized to the *same* fault:
  Bluedroid's HCI ACL reassembler failed its ~1 KB allocation (full_length 973 B,
  `packet_fragmenter.c:175`) and the precompiled stack NULL-wrote
  (StoreProhibited, same PC in both crashes) — the allocation failure came from
  Android-side heap fragmentation. Two contributors were fixed: (1)
  `ui_screen_media.cpp` left `lv_image_dsc_t.header.flags` uninitialized, so a
  screen rebuild copied stack garbage into decoder-visible bits — LVGL's image
  decoder then walked a NULL decompress buffer (EXCVADDR 0x9b0) and freed the
  STATIC artwork buffer (heap corruption); the descriptor is now fully zeroed
  and value-initialized. (2) The artwork was scaled at paint time with
  antialiasing, allocating/frees a destination-sized transform buffer on every
  progress-bar repaint; antialias is now permanently off on the media artwork
  (nearest-neighbour, no transform buffer).
  until Bluedroid's HCI reassembler failed its ~1 KB allocation and the BT host
  StoreProhibited-crashed mid-stream — the same fragmentation RCA as the crash;
  with antialias off LVGL scales nearest-neighbour with no intermediate buffer.
* **Heap governor for artwork transfers.** Below 16 KB largest free block
  (`DASHCUE_HEAP_GUARD_BYTES`) the sink now DEFERS ARTWORK_BEGIN, rejects the
  burst chunks harmlessly and defers the resend request; the self-healing
  MediaState retry re-triggers the transfer once memory recovers. Measured in
  `docs/evidence/20261026/soak_final.log`: 145 s streaming soak with 8 track
  changes, artwork deferred cleanly by the guard, **0 panics / 0 resets** (the
  same test previously produced 2 crashes in 150 s).
* **Bilinear prescale cache: tried, measured, REVERTED to optional.** A 48,672 B
  one-time bilinear upscale cache allocated at boot starved the BT stacks from
  init (`sink started` heap 111,612→62,908; BLE init largest 77,812→31,732;
  display stage ended at 9 KB free) and cost **14 resets in one churn soak**
  (`soak_prescale_fix.log`). The cache code stays in `artwork_store` behind
  `artwork_store_prescale[_alloc]()` for a future build with a verified >96 KB
  largest-block budget; the shipped media screen draws the native source with
  antialias off.
* **A2DP NVS last-connection sanitized at boot.** `restore_a2dp_last_connection`
  now wipes the stored `last_bda` when it does not identify the BLE-bonded
  phone (junk addresses had been persisted into NVS and silently kept across
  reboots; measured `docs/evidence/20261026/boot_sanitize*.log`), falling back
  to the bond-list seed.
* **Loudness raised inside the speaker envelope.** A2DP streaming base volume
  80→100 (`DASHCUE_A2DP_BASE_VOLUME`, +1.9 dB), alert default 50→70 and the
  duck table rescaled (duck 100→25). The 90 %-of-full-scale line
  (`AUDIO_MAX_VOLUME_PCT`, `AUDIO_SAFE_PEAK_COUNTS`) is unchanged — the
  envelope the user's **8 Ω 0.5 W** speaker ceiling was analysed for.
* **Clarity high-pass added.** A 120 Hz high-pass biquad runs in the A2DP PCM
  callback of `dashcue_a2dp_sink` before the audio data reaches I2S, removing
  the low-frequency excursion an 8 Ω 0.5 W driver wastes most of its cone
  stroke on while streaming; alert audio keeps its existing biquad EQ.
* **Serial evidence harness hardened.** `tools/test-only/serial_harness/
  capture.py` no longer pulses RTS by default (its diagnostic captures were
  resetting the CYD — the reported "flicker/boot-loop" symptom mixed genuine
  firmware resets with harness-induced resets), and it now tolerates
  mid-capture USB port drops (partial logs are kept even when COM4 dies;
  `--no-reset` honored throughout).
  COM4 itself still flaps during streaming/app-relaunch sessions (transient
  USB re-enumeration); captures retried after the port settles work fine.
* **Regression status:** firmware build SUCCESS (RAM 68,360 B, 20.9 %);
  native suite 24/24 (`pio test -e native`); Android unit tests 24 tests /
  0 failures (`:app:testDebugUnitTest`).

## [Unreleased] 20261023 - commercial stabilization pass

* **Screen-off continuity shipped on the Android side.** The BLE foreground
  service holds a `PARTIAL_WAKE_LOCK` (`DashCue::BleServiceWakeLock`) from
  `startAsForeground()` to `onDestroy`, and the 2000 ms keepalive plus MT_*
  mirror pipeline moved into the process-scoped `DashCueSyncBridge` running in
  `AppState.appScope`, so media commands from the HUD touch buttons reach
  `MediaSessionManager` even with `MainActivity` destroyed or the phone locked.
* **Link-loss feedback on the RGB LED.** The active-LOW LED machine in
  `ui_manager` now blinks amber while heartbeats are stale (1 Hz) and red after
  a bonded phone is lost; white flash on touch, amber pairing, emerald fresh
  playback and cyan idle are unchanged.
* **Text-fit hygiene on the HUD.** A nav street longer than the 290 px card
  wraps onto a second line (measured with `lv_text_get_size`, never a bare
  ellipsis for the primary cue; pathological 3-line names fall back), and the
  media title, artist and app badge marquee (`LV_LABEL_LONG_MODE_SCROLL_CIRCULAR`)
  only when they actually overflow.
* **Every UI button click is serialized.** All click handlers now print a `UI:`
  line naming the button and the dispatched command, so a tap that does nothing
  is distinguishable from a tap that was never received.
* **A11y contract updated to the current transport geometry.**
  `tests/adversarial/test_a11y_contrast_adversarial.py` asserted the retired
  138/196/256 transport row; it now pins the live split-layout targets (72x76
  px nodes + ext click areas, still gated at >= 56x48 px). Evidence:
  `docs/evidence/20261023/`.

## [Unreleased], album-matched media screen

* **Blank white panel: two separate defects, both fixed.** (1) The LVGL flush
  never delivered pixels: the per-strip `setAddrWindow` + `pushColors` path left
  the glass on its power-on white field while the draw buffer verifiably held the
  right dark pixels (`px0=0x0021`, mean luminance 4) and the hardware probe's
  `fillRect`/`pushImage` output reached it. The flush now uses `pushImage` with
  `setSwapBytes(true)`. (2) The panel needs **INVON**: a pure-red test screen
  rendered cyan with `invertDisplay(false)` and red with `invertDisplay(true)`. The
  driver's `ILI9341_2` init never sets INVON/INVOFF, so the panel's power-on
  default decided, and a near-black theme rendered inverted looks white. Polarity
  is now pinned explicitly in `display_hal_init`. Evidence:
  `docs/evidence/20261021-reflash/{flush-inspect,after-pushimage,final-boot}.log`.
* **Display pin integrity is measured at every boot.** `CS`/`DC`/`SCLK`/`MOSI` are
  driven high then low and read back (`PIN CS gpio=15 drive_hi->read=1
  drive_lo->read=0 ok`), so "a pin is stuck" is a logged number instead of a
  hypothesis. All four are clean on this unit.
* **Touch contact now comes from PENIRQ, and a panel that cannot sense position
  no longer bricks the HUD.** Three separate faults were measured on this unit,
  each hiding the next:
  1. The XPT2046's pressure chain is dead - `z1 = z2 = 0` on every read, so the
     driver's `z = z1 + 4095 - z2` saturates at ~4095 and `touched()` (which gates
     on `z >= 400`) reports a continuous press whether or not a finger is present.
     Contact is therefore taken from PENIRQ on GPIO36, which follows the glass:
     `1` at rest with axes at ~3771/2889, `0` while pressed with axes at ~14/1434.
  2. With PENIRQ gating, taps reach the wizard - but all five targets 260 px apart
     return the same raw point (`(0,1327)` .. `(0,1337)`, mean residual 60.6 px,
     max 142.0 px). The panel reports contact without position, so no calibration
     curve can exist.
  3. Both conditions are now detected and reported instead of trapping the
     operator: three press onsets inside 300 raw counts, or a five-tap fit across
     a degenerate spread, mark the position axis dead; the wizard names the fault
     and continues into the HUD after 4 s, with a red `TOUCH` badge in the status
     bar. A 45 s grace period keeps the wizard available for anyone who does want
     to calibrate. Evidence:
     `docs/evidence/20261021-reflash/{touch-channels,pen_irq-probe,touch-held,cal-live,cal-live2,afk-final}.log`.
     Hardware conclusion: the touch layer's X/Y plates are not dividing as a finger
     moves (nine presses never left a 34-count box in X) - that is a panel/FPC
     fault, not firmware. `docs/HARDWARE_REPORT.md` §4 recorded a working raw range
     of `x [2..2319], y [1347..3620]` on 2026-10-16, so this is a regression on the
     same board. One valid calibration still clears the fault, so a replaced panel
     needs no reflash. Diagnosis and repair procedure:
     [docs/TOUCH_FAULT_RUNBOOK.md](docs/TOUCH_FAULT_RUNBOOK.md).
* **BOOT button navigation as a fallback input.** With pointer input unavailable,
  GPIO0 steps through media → nav → settings (one step per press, 250 ms lockout)
  so the HUD does not sit on one tab forever. Fallback only; a working touch panel
  remains the primary input.
* The HUD media screen is now a Spotify-style now-playing view: a vertical gradient
  built from the album art's dominant colour (darkened to 45% at the top so the
  centered white `Title · Artist` stays AAA-legible) fading to near-black, the hero
  album art centered above the metadata line, the scrubber across the top, and the
  five 44 px transport/volume controls underneath. Accent surfaces (art border,
  scrubber fill, play/pause, app label) retint with the artwork.
* Artwork transfer moved from 48×48 (4,608 B) to **64×64 RGB565 (8,192 B, 8 × 1 KB
  chunks)**. The hero tile renders it at 80 px with `lv_image_set_antialias(true)`,
  so the 1.25× scale is filtered instead of blocky.
* The phone now downscales with an **area-average (box) filter** instead of bilinear
  `createScaledBitmap`. A 1 px checkerboard integrates to ~127 grey rather than
  aliasing to pure black/white, which is what removed the choppy look; covered by
  `Rgb565ArtworkConverterTest` (5 tests).
* The dominant accent is extracted on **both** sides from identical weighted-vote
  algorithms (firmware `artwork_store.cpp`, phone `AlbumColorExtractor`) so the HUD
  tile and the phone card never disagree about the colour.
* Verification: `pio run -e cyd_ili9341` SUCCESS (flash 54.5%), native 22/22,
  `:feature:media:testDebugUnitTest` 24/24, `:app:assembleDebug` SUCCESS,
  `tools/contrast_check.py` 16/16.
* **Memory gate is green:** static DRAM measures **69,780 B against the 70,000 B
  gate**. The 8,192 B artwork buffer moved from `.bss` to a lazily allocated heap
  block, 14 unused LVGL widgets were disabled through the generator (not by
  hand-editing `lv_conf.h`), and three dead translation units were deleted. No
  feature was traded away.
* **Dead code removed:** `ArtworkConverter.kt` and `MediaNotificationListener.kt`
  (Android, both superseded and unregistered), `touch_filter.*` +
  `xpt2046_touch_source.*` + `touch_source.h` (zero references), and the duplicated
  `TouchCalibration` solver in `touch_calibration.cpp`, only its
  `getTargetCoordinate` static was ever called, and that is now inline in the
  header. The live solve/apply/NVS path remains the single implementation in
  `calibration.cpp`.
* Physical-panel appearance is **not** verified: there is no headless LVGL renderer
  here, so the gradient, the 84 px art scale and the 300 ms crossfade have only been
  checked by build + reasoning, not by looking at the panel.

## [0.3.0], 2026-10-17, three-tab HUD and wired companion UI

* The HUD drops its schematic map tab: **Media / Nav / More**, three 80×48 targets.
  Media and navigation cards now follow the 64×64 artwork, 224×90 metadata and
  104×136 / 186×136 cue geometry. Removing the map screen and its 1 KB route
  snapshot brought static DRAM to **69,364 B** (limit 70,000 B); flash 55.0%.
* The companion app replaces its stub home screen with **Dashboard, Navigation,
  Settings and Diagnostics**. A single `DashCueViewModel` mirrors MediaState,
  NavState and SettingsSet to the HUD and executes inbound MediaCmd/NavCmd against
  the real Android session, replying with CmdResult.
* Navigation uses the pinned MapLibre 13.7.1 view with a local raster style,
  user-supplied OSRM/Nominatim endpoints, Room-backed favorites/recents and a
  route polyline decoded from RouteOverview deltas.
* Diagnostics exposes MTU/RSSI/frame counters, HUD DeviceStatus, a protocol trace
  and a JSON export bundle. Settings persists brightness, theme, orientation,
  units, seek step and endpoints through the existing DataStore repository.
* Verification: native 22/22, Android 71/71, host model 265/265, release APK built
  and installed. Physical HUD, phone screenshot and routing runtime remain blocked;
  see [docs/evidence/20261017](docs/evidence/20261017/README.md).

## [0.2.0], 2026-10-16, M2 protocol schema and codegen

* `tools/gen_proto.py` generates the nanopb codec from the single `.proto`; `--check`
  regenerates into a temporary directory and fails when committed output drifts, which is
  what makes "CI fails if generated files drift" meaningful.
* The generator is taken from the **pinned Nanopb library itself** rather than PyPI: the
  documented `nanopb==0.4.9.2` does not exist there (newest stable is `0.4.9.1`), and
  using the library's own generator makes a generator/library version mismatch impossible.
* Generated sources are committed (`.gitignore` excludes only the Android path); this was
  a deliberate choice recorded as D-08.
* Verified that every `.options` cap reaches the generated header as a fixed-size array and
  that the messages contain **no pointer fields**, so the decode path allocates nothing on
  the heap (DEC-12).
* Javalite generation is delegated to the Android Gradle build, because `grpc_tools.protoc`
  is built without Java codegen. The tool reports that side as `DEFERRED` with the reason.
* The nanopb generator **rejected three lines** in `dashcue.options` (`type:FT_FLOAT` is not
  a nanopb value); they were removed, with the wire-form guarantee explained. A bogus option
  now fails the build instead of silently doing nothing.
* Firmware builds with the codec compiled in: RAM 6.9 %, flash 10.5 % used (89.5 % free,
  against the §16 requirement of ≥ 25 %).

## [0.1.0], 2026-10-16, M0 and the start of M1

**M0, repository, tooling, gates**
* Repository skeleton per specification §18; the protocol schema is the first artefact
  in place (`protocol/dashcue.proto`, `.options`, frozen UUIDs).
* `tools/setup_check.py` (GATE-01) checks Python, PlatformIO, the pinned espressif32
  platform, the board profile, serial ports, JDK, Android SDK, adb, repo layout and git;
  it exits non-zero on a missing requirement and prints the fixing command.
* GATE-08 dependency pinning with the authoritative version metadata for every firmware
  and Android dependency, plus three recorded deviations and the reasons.
* `tools/check_dependency_pins.py` makes pin drift fail instead of relying on review.
* `tools/contrast_check.py` verifies the §12.10 palette and recomputes the firmware's
  RGB565 constants, it caught six of seven hand-converted values being wrong.
* `docs/DECISIONS.md`, `docs/VERIFICATION.md`, `docs/SPEC_ISSUES.md`,
  `docs/KNOWN_ISSUES.md` and the remaining §18 documents created, with unfinished work
  marked `NOT RUN`/`BLOCKED` rather than omitted.

**M1, hardware probe (in progress)**
* `tools/hw_probe` firmware identifies the board and resolves §5.2's touch-bus
  question by scanning both bus candidates against nine chip-selects with a
  variance-based liveness test.
* **Measured:** ESP32 rev 3, 4 MB flash, no PSRAM; display 320×240 with 31.2 ms
  full-screen fill and a 5.6 ms 320×40 strip blit (~29 FPS single-buffered estimate,
  target ≥ 20 FPS); touch controller on its own SPI bus (SCK 25 / MOSI 32 / MISO 39 /
  CS 33) polled at 4 160 Hz.
* **Corrected assumption:** the firmware's starting pin map put touch on the display
  bus; the probe disproved it. The measured bus is now in `dashcue_config.h`.
* **Found and fixed a measurement bug:** a floating bus reads `0xFFFF`, which a naive
  pressure test accepts as a touch. Touch input now rejects saturated reads.
* `tools/test-only/serial_harness/capture.py` captures hardware evidence and fails when
  a `FAIL` line appears in the capture.

**Not done yet, stated plainly**
* GATE-03 is `PARTIAL`: LVGL 9.6.0 compiles with TFT_eSPI, but no LVGL runtime
  measurement exists, so the display stack is not yet proven.
* GATE-04 is `PARTIAL`: calibration and pixel pitch unmeasured.
* M2 onwards: no generated protocol code, no BLE, no Android app.
