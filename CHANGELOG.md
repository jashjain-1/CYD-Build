# Changelog

Milestone-scoped, newest first. Every entry names the evidence that backs it, so a
claim in this file can be checked rather than believed.

## [Unreleased], album-matched media screen

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
