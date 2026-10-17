# Decisions and deviations

This file is the living counterpart to §3 of the specification. **The decisions in
§3 are not restated here** — they are already fixed and are not re-litigated. What
this file records is:

1. the outcomes of the verification gates §3 said would decide something, and
2. every place where reality forced a deviation, with what, why, evidence and impact.

Rule 2 of §24 requires every deviation to be recorded here. An empty deviations
list would be a warning sign, not an achievement, on a project that has already
touched real hardware.

---

## 1. Gate-resolved decisions

### DEC-04 / §5.2 — which SPI bus carries the touch controller

§5.2 offered two hypotheses. GATE-02 measured both and resolved it:

| Hypothesis | Pins | Result |
|---|---|---|
| touch shares the display bus | SCK 14 / MOSI 13 / MISO 12, CS 33 | **rejected** — no response on any of nine CS candidates |
| touch has its own bus | SCK 25 / MOSI 32 / MISO 39, CS 33 | **confirmed** — controller answers, X channel drifts |

*Decision taken:* `firmware/include/dashcue_config.h` carries the measured pin map
and the firmware must not read touch from the display bus. Calibration data is
therefore revision-bound, so the revision string is compiled in and sent in
`HELLO.hw_revision`.
*Evidence:* [docs/HARDWARE_REPORT.md](HARDWARE_REPORT.md) §3,
[docs/evidence/20261016/TC-HW-01/hw_probe_scan.log](evidence/20261016/TC-HW-01/hw_probe_scan.log).

### GATE-02 — display controller identity

RDDID (0x04) returns `0x0000` on this panel, so ILI9341 vs ST7789 **cannot be
determined** from the board. The ILI9341 configuration drives the panel correctly,
which is evidence that the configuration works — not evidence about the silicon.

*Decision taken:* {DEC-03} stands (TFT_eSPI + LVGL 9). The board profile keeps the
ILI9341 driver configuration, the `cyd_st7789` environment exists for revisions
that need it, and no document claims the panel *is* an ILI9341.
*Evidence:* [docs/HARDWARE_REPORT.md](HARDWARE_REPORT.md) §2.

### GATE-02 — display orientation

Confirmed **correct** by the operator on 2026-10-16 (rotation 1, landscape). No 180°
correction is needed for the default mount; a rotated-180 option stays a user
setting per §12.6.

---

## 2. Deviations from the decision register

### D-01 — `compileSdk 36`, not the newest available

* *What:* DEC-09 says compileSdk/targetSdk should be the latest stable API level at
  project start. That is not what is built.
* *Why:* the newest stable Compose BOM (`2026.09.00`) pulls Compose 1.12, which
  requires `compileSdk 37`. This host has `platforms;android-36` installed and
  licensed; SDK 37 is not present.
* *Evidence:* `docs/evidence/20261016/GATE-01/setup_check.txt` records the installed
  platform; the version derivation is in [DEPENDENCIES.md](DEPENDENCIES.md) §2, D-01.
* *Impact:* none on behaviour the v1 UI needs. Revisit when an SDK 37 host exists.
  `targetSdk` stays 36 too, so runtime behaviour is unchanged by this choice.

### D-02 — AGP `8.13.2`, not `9.4.1`

* *What:* the newest stable Android Gradle Plugin is 9.4.1; the build pins 8.13.2.
* *Why:* AGP 9 enables built-in Kotlin and its new DSL is documented as not
  compatible with applying `org.jetbrains.kotlin.android` by default. The modules
  are written against the documented Kotlin-plugin setup.
* *Impact:* stays on the supported 8.x line for API 36. This is a deliberate deferral,
  not an oversight, and it is revisited when the build files are migrated on purpose.

### D-03 — Kotlin `2.3.21`, not `2.4.21`

* *What:* newest stable Kotlin is 2.4.21; the build pins 2.3.21.
* *Why:* Room needs KSP, and KSP's newest published version is 2.3.12, which pairs
  with Kotlin 2.3.x. Kotlin 2.4.21 would leave the annotation processor without a
  matching release.
* *Evidence:* KSP artifact metadata, 160 versions, newest `2.3.12`
  ([DEPENDENCIES.md](DEPENDENCIES.md) §3, D-03).
* *Impact:* none for the code this project writes.

### D-04 — DEC-03's fallback (LVGL 8.4) is no longer viable

* *What:* DEC-03 says: if GATE-03 fails with LVGL 9 + TFT_eSPI, fall back to
  "LVGL 8.4 LTS-line".
* *Why:* LVGL's own version-support policy lists **8.4 support as ended on
  2025-03-20**. Falling back to an unmaintained branch on a security-relevant device
  is worse than staying on a 9.x patch release.
* *Decision taken:* the fallback for a GATE-03 failure becomes the newest working
  **LVGL 9.x** patch, down to 9.0 if necessary. This is a proposed change to §3 and
  is logged in [SPEC_ISSUES.md](SPEC_ISSUES.md) as SI-01.
* *Impact:* the spec's stated fallback no longer matches reality; the intent (one
  display stack, a maintained release) is preserved.

### D-05 — board profile file name

* *What:* §5.2 says results are written to `boards/cyd_<rev>.json`.
* *Decision taken:* the file is `boards/cyd_2432s028r.json` — the revision token is
  the board model plus the pin-map revision, which is the smallest name that
  distinguishes the revisions we can actually tell apart (they differ by touch bus
  and panel controller, not by a vendor revision number we can read).
* *Impact:* none.

### D-06 — the hardware probe is a separate PlatformIO project

* *What:* §4 says board identification is done "via `tools/hw_probe` firmware".
* *Decision taken:* it is a standalone PlatformIO project there, deliberately not an
  environment of `firmware/`. If the probe fails, the fault is hardware; if it were an
  environment of the product it would be entangled with the product's board profile,
  library set and (later) LVGL configuration.
* *Impact:* the probe shares `firmware/include/dashcue_config.h` and
  `dashcue_version.h` through an include path, so there is still one pin map.

### D-07 — the nanopb **generator** comes from the pinned library, not from PyPI

* *What:* DEC-12 requires nanopb on the firmware side, and §18 requires `tools/gen_proto.py`
  to generate from the single `.proto`.
* *Why:* the documented pip pin `nanopb==0.4.9.2` **does not exist on PyPI** (newest stable
  is `0.4.9.1`); the PlatformIO registry calls the same release `0.4.92`. Separately,
  `grpc_tools.protoc` ships no Java codegen, so javalite cannot be generated by any
  local `protoc`.
* *Decision taken:* the generator is taken from
  `firmware/.pio/libdeps/*/Nanopb/generator/`, so generator and firmware library are the
  same version by construction; javalite generation is delegated to the Android Gradle
  build, which bundles a protoc that does include Java. `gen_proto.py` reports that side
  as `DEFERRED` with the reason rather than claiming it ran.
* *Evidence:* generated output in `firmware/src/protocol/generated/`; caps verified in the
  header; `gen_proto.py --check` compares committed output against fresh output.
* *Impact:* `pio run` must have run at least once before codegen (the tool says so).

### D-08 — generated protocol sources are committed

* *What:* §18 says "CI fails if generated files drift", which is only meaningful if the
  generated files are in the tree.
* *Decision taken:* the nanopb outputs are committed and `.gitignore` excludes only the
  Android-generated path (Gradle regenerates those).
* *Impact:* review diffs include generated code; that is the cost of the drift check the
  specification asks for.

---

## 3. Additions made without changing any §3 decision

- **Directional test doubles guard.** `dashcue_config.h` fails the build if
  `DASHCUE_TEST_DOUBLES` is defined in firmware sources. DEC-26 says test doubles
  live only in test sets; this makes it a compile error rather than a review note.
- **`tools/check_dependency_pins.py`.** GATE-08 says dependencies are pinned; this
  script makes drift fail instead of relying on review.
- **`tools/contrast_check.py`.** §12.10 says the palette is "verified"; this is the
  verification, and it also recomputes the RGB565 constants in the firmware header so
  a hand-conversion mistake cannot survive.
- **Saturated-read rejection.** The M1 probe found that a floating touch bus reads
  `0xFFFF`, which a naive pressure test accepts as a press. Touch input therefore
  rejects `z ≥ 4090` and out-of-range `x`/`y` as *no signal*. This is an addition in
  the spirit of Rule 10 (calibration safety), recorded because it came from a
  measurement rather than from the spec.

---

## 4. Decisions taken during M1/M3 bring-up (all hardware-verified 2026-10-16)

- **D-09 — the BLE stack is Bluedroid, not NimBLE.** DEC-05 named NimBLE and a fallback;
  the fallback is now the reality. The prebuilt Arduino-ESP32 3.x SDK ships a `libbt.a`
  containing **zero NimBLE objects**, so `NimBLEDevice::init()` blocks forever (no panic, no
  message) — which is exactly what the first GATE-05 attempt looked like. `ble_peripheral`
  was re-implemented against `BLEDevice::`/`esp_ble_gatts` with the same public API.
  *Evidence:* [TC-HW-01/fw_ble_mem_release.log](evidence/20261016/TC-HW-01/fw_ble_mem_release.log).
  *Impact:* the pinned NimBLE-Arduino dependency is unused and should be removed from
  `platformio.ini` in the same change that re-states GATE-05's procedure.

- **D-10 — the controller is brought up BLE-only, after releasing classic-BT memory.**
  The Arduino core enables the controller in BTDM mode (classic + BLE), reserving memory a
  BLE-only HUD never uses. `esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)` is only
  safe **before** the controller starts *and* if it then runs `ESP_BT_MODE_BLE`; releasing it
  and still starting BTDM leaves an uninitialised classic queue (`vQueueDelete` assert), which
  is what the first attempt at this hit. Gain ≈ 15 KB.
  *Evidence:* [TC-HW-01/fw_ble_mem_release.log](evidence/20261016/TC-HW-01/fw_ble_mem_release.log)
  (`classic-BT mem release rc=0`, `controller BLE-only`).

- **D-11 — one UI stack.** `src/ui/` (UiManager) is the UI: it owns navigation, the tab bar,
  the splash and the single `lv_timer_handler()` caller. A second, complete set of seven
  screens (`media_ui/`, `navigation_ui/`, `settings/`, `diagnostics/{diag,safemode}_screen`,
  `display/status_bar`, `touch/calibration_screen`) was compiled in and eagerly created by
  `display_hal_init()` while `display_hal_start_ui_task()` had **no caller** — ~58 KB of
  never-displayed screens that exhausted the heap and rebooted the board
  (`lv_obj_allocate_spec_attr … Assertion spec_attr != NULL` → `abort()`).
  Those sources were deleted; `display_hal` is now the panel/input/metrics layer only.
  *Evidence:* [TC-HW-01/fw_heap_budget.log](evidence/20261016/TC-HW-01/fw_heap_budget.log) (crash)
  vs [TC-HW-01/fw_single_ui_stack.log](evidence/20261016/TC-HW-01/fw_single_ui_stack.log) (boots).

- **D-12 — LVGL's clock comes from a tick callback, and the metrics are measured.**
  The only `lv_tick_inc()` lived in the dead `display_hal` ui_task, so in the live path LVGL's
  time never advanced and every LVGL timer/animation was inert. `display_hal_init()` now calls
  `lv_tick_set_cb()`, which makes the clock independent of which task drives LVGL. The FPS /
  frame-time getters previously returned hardcoded defaults (25 / 18 / 24 / 32) when
  unmeasured; they now report pixels actually pushed to the panel and the UI task's own
  iteration time, and `main.cpp` prints them in the STATS line.
  *Impact:* GATE-03 must be re-run to get a real animation FPS number; the previous getters
  could not have produced one.

- **D-13 — BLE is initialised before the UI.** The BT controller and host need multi-KB
  contiguous blocks; a heap already broken up by ~46 KB of LVGL objects cannot serve them
  (`esp_bluedroid_init: rc=-1`, which on the phone side is indistinguishable from "no HUD
  here"). `setup()` therefore orders BLE first, then display/touch/UI.
  *Evidence:* [TC-HW-01/fw_bluedroid_init.log](evidence/20261016/TC-HW-01/fw_bluedroid_init.log).

- **D-14 — the panel's controller identity remains UNKNOWN.** It does not answer RDDID
  (`0x0000`) and is driven with the `ILI9341_2_DRIVER` configuration that GATE-02 found
  working. An operator report of "8 colour blocks" on the glass is recorded as
  **unattributed**: a TFT keeps showing its last frame, so that pattern is equally consistent
  with (a) the probe's own colour-bar image still being the last thing drawn, (b) a panel
  revision needing the `cyd_st7789` environment, or (c) the product firmware never having
  painted a full frame. §4.2 of `docs/NEXT_AGENT_HANDOFF.md` says how to tell them apart
  with the render metrics plus eyes on the glass. It is **not** to be guessed.
