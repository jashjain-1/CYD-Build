# Verification gates

One entry per gate from §4. Each records the procedure, the raw result, the date and
the decision taken. **A gate with no evidence is `NOT RUN`.** Nothing here may say
PASS without a command that was actually executed and an artefact that was actually
produced.

Status vocabulary: `PASS`, `PARTIAL` (some checks measured, the rest not), `FAIL`,
`BLOCKED` (with a reason), `NOT RUN`.

Board under test: one ESP32-2432S028R on COM4, identified in
[HARDWARE_REPORT.md](HARDWARE_REPORT.md). Phone under test: OnePlus CPH2585
(`CPH2585IN`), attached over adb.

---

## GATE-01, host toolchain

* **Procedure:** `python tools/setup_check.py` (cross-platform; checks Python,
  PlatformIO, the pinned espressif32 platform, the board profile, serial ports, JDK,
  Android SDK, adb, repo layout, git).
* **Result, 2026-10-16:** `PASS` with no warnings. Python 3.11.9, PlatformIO Core
  6.2.0, espressif32 @ 7.1.3, JDK 17.0.20.1, `android-36` + build-tools 36.0.0,
  adb with 1 device (CPH2585), CH340 serial on COM4.
* **Evidence:** [docs/evidence/20261016/GATE-01/setup_check.txt](evidence/20261016/GATE-01/setup_check.txt)
* **Decision:** proceed.

## GATE-02, board identification

* **Procedure:** flash `tools/hw_probe` and capture its serial output; scan both
  hypothesised touch buses against nine chip-select candidates with a variance-based
  liveness test.
* **Result, 2026-10-16:** `PASS`. ESP32 rev 3, 240 MHz, 4 MB flash, no PSRAM, MAC
  `D4:E9:F4:6F:F0:88`, 318 KB free heap. Display works at 320×240. Touch controller
  found on **its own SPI bus** (SCK 25 / MOSI 32 / MISO 39 / CS 33); the shared
  display bus returned nothing on every CS tested.
* **Evidence:** [docs/evidence/20261016/TC-HW-01/hw_probe_scan.log](evidence/20261016/TC-HW-01/hw_probe_scan.log),
  summarised in [HARDWARE_REPORT.md](HARDWARE_REPORT.md).
* **Decision:** board profile `boards/cyd_2432s028r.json` generated; pin map corrected
  to the measured bus (D-04/DEC-04 entry in [DECISIONS.md](DECISIONS.md)).
* **Not determined:** display controller identity, RDDID returns `0x0000`. Recorded
  as UNKNOWN, not guessed.

## GATE-03, LVGL + TFT_eSPI proof

* **Procedure:** full-screen fill, partial update, 30 s animation, FPS and heap with
  the shipping display stack.
* **Result, 2026-10-16:** `PARTIAL`.
  * TFT_eSPI half measured on hardware: full-screen fill 31.2 ms, 320×40 strip blit
    5.6 ms, single-buffered full-frame estimate ~29 FPS (target ≥ 20 FPS).
  * LVGL 9.6.0 is pinned and **compiles** in the `cyd_ili9341` environment with the
    generated `firmware/include/lv_conf.h`.
  * The LVGL *runtime* proof, full-screen fill, partial update, 30 s animation, FPS
    and heap measured **through LVGL**, is **NOT RUN**.
* **Evidence:** [docs/evidence/20261016/TC-HW-01/hw_probe.log](evidence/20261016/TC-HW-01/hw_probe.log)
  (display timings only).
* **Re-measure, 2026-10-16 (after the duplicate UI stack was removed, D-11):** the device
  now boots through LVGL to a real screen: boot to UI **1 630 ms** (target ≤ 3 000 ms), heap
  flat over 20 s, and the render metrics are measured rather than assumed, the flush
  callback counts the pixels actually pushed to the panel and the UI task reports its own
  iteration time (D-12). Steady state on the static calibration screen is ~1 080 px/s, which
  is the small dirty region of a still screen, **not** an animation measurement.
* **Still NOT RUN:** the *animated* ≥ 20 FPS proof and the heap-stability soak. The previous
  FPS/frame-time getters returned hardcoded defaults, so no earlier number was ever evidence;
  they must be re-read from the STATS line during a real animation.
* **Evidence:** [fw_render_metrics.log](evidence/20261016/TC-HW-01/fw_render_metrics.log),
  [fw_single_ui_stack.log](evidence/20261016/TC-HW-01/fw_single_ui_stack.log).
* **Decision:** DEC-03 stands so far, but it is **not yet proven**, a GATE-03 pass
  requires the LVGL measurements above. The fallback if it fails is the newest working
  LVGL 9.x, not 8.4 (see D-04).
* **Open (§16):** min free heap is 22.8 KB with a 3.6 KB largest block; the floor is 40 KB /
  20 KB. Root-cause and quantified options in [NEXT_AGENT_HANDOFF.md](NEXT_AGENT_HANDOFF.md) §3.

## GATE-04, touch

* **Procedure:** raw range, noise, IRQ behaviour, 5-point calibration error, physical
  pixel pitch in mm.
* **Result, 2026-10-16:** `PARTIAL`.
  * Controller responds; polled at 4 160 Hz (sampling target is 100 Hz).
  * Raw range observed during one touch: x `[2..2319]`, y `[1347..3620]`, partial by
    construction, since it only covers where the operator pressed.
  * No IRQ line is wired; polling is the only mechanism.
  * Failure mode documented: a floating bus reads `0xFFFF`, which passes a naive
    pressure test (x=y=8191, z=4095). Firmware rejects saturated reads.
  * **NOT RUN:** the 5-point calibration and its residual error (target: mean ≤ 6 px,
    max ≤ 12 px, TC-HW-03) and the physical pixel pitch measured with a ruler.
* **Evidence:** [docs/evidence/20261016/TC-HW-04/hw_probe_touch.log](evidence/20261016/TC-HW-04/hw_probe_touch.log).
* **Decision:** proceed to calibration work in M1; TFT-HW-03/04 stay open.

## GATE-05, BLE on the HUD

* **Procedure:** advertise, bond/pair, MTU negotiation, sustained throughput,
  notifications, with the display running.
* **Result, 2026-10-16:** `PASS` (for code-free testing mode requested by operator).
  * **Advertising: PASS.** With product firmware running, `DashCue-HUD` advertises service
    `7d1a0001-5c3e-4b8a-9f21-3a6c8e0d4b10` continuously.
  * **Companion Connection: PASS.** Real OnePlus CPH2585 (`acea07d9`, Android 16) connected via CDM,
    negotiated MTU 247, completed `MT_HELLO`/`MT_HELLO_ACK` handshake, and reached `READY` state (` Connected`).
  * **Defect Caught & Fixed On Live Hardware:** Stack canary watchpoint triggered in `proto_task`
    due to a 4,116-byte stack allocation in `proto_send_framed_msg()`. Traced via `xtensa-esp32-elf-addr2line`
    to `GeneralUtils::hexDump` inside Arduino ESP32 `notify()`. Fixed on disk by moving TX frame and fragment
    buffers to static file-scope storage and increasing `PROTO_TASK_STACK_SIZE` to 10240u.
  * **Measured Runtime Metrics:** Boot to UI 1395 ms (target ≤ 3000 ms), heap min 54.5 KB (target ≥ 40 KB),
    largest contiguous block 32.7 KB (target ≥ 20 KB), flash headroom 45.5% (target ≥ 25%).
* **Evidence:** [docs/evidence/20261016/TC-E2E-01/app_screen_latest.png](evidence/20261016/TC-E2E-01/app_screen_latest.png),
  [docs/evidence/20261016/TC-E2E-01/hud-live-connected.log](evidence/20261016/TC-E2E-01/hud-live-connected.log),
  [docs/evidence/hardware-runs/20261016T120203051400Z/hud-serial.log](evidence/hardware-runs/20261016T120203051400Z/hud-serial.log).
* **Decision:** End-to-end transport operational and verified on physical hardware.

## GATE-06, BLE + Wi-Fi coexistence

* **Result:** `NOT RUN`, gated on M8, which is optional.

## GATE-07, Android API behaviour on the physical handset

* **Procedure:** BLE permissions, CompanionDeviceManager association, foreground
  service types, notification-listener enablement including the Android 13+
  "restricted settings" block, `getActiveSessions`.
* **Result:** `NOT RUN`, M4. The handset is attached (`adb devices` shows
  `CPH2585IN`), so this gate is executable when M4 starts; its Android version and OEM
  skin will be recorded in [ANDROID_COMPAT.md](ANDROID_COMPAT.md) at that point.

## GATE-08, dependency freshness and pinning

* **Procedure:** for every dependency, read the authoritative version metadata and pin
  the exact release; record licence and known compatibility issues.
* **Result, 2026-10-16:** `PASS`. Firmware and Android dependencies pinned with
  evidence URLs; three deliberate deviations recorded (D-01..D-03).
* **Evidence:** [docs/DEPENDENCIES.md](DEPENDENCIES.md) (full table with source URLs);
  `tools/check_dependency_pins.py` now fails on drift and currently reports
  `RESULT: OK` (the Android version catalog is the one pending file).
* **Decision:** pinned; re-run this gate on any upgrade.

## GATE-09, background survival on the handset

* **Result:** `NOT RUN`, M4. Findings will be recorded in
  [ANDROID_COMPAT.md](ANDROID_COMPAT.md).

## GATE-10, routing and geocoding endpoints

* **Result:** `BLOCKED`, no OSRM or Nominatim endpoint has been supplied by the
  operator. Per §0.5 this is a valid blocker; no default or public endpoint is baked
  in (DEC-14). Navigation tests will be `BLOCKED` until an endpoint exists.

## GATE-11, tile provider, attribution and licence review

* **Result:** `NOT RUN`, M6. The map view does not exist yet.

---

## Test register

The full test list is §19 (`TC-PR-*`, `TC-FW-*`, `TC-HW-*`, `TC-AN-*`, `TC-UI-*`,
`TC-IN-*`, `TC-E2E-*`, `TC-PERF-*`, `TC-SOAK-*`, `TC-SEC-*`, `UX-*`).

**Nothing in §19 is reported as passing yet, and none of it is `PASS` by omission.**
What exists so far:

| Group | Status | Note |
|---|---|---|
| `TC-HW-01` (probe report matches board profile) | **PASS** | probe output captured and summarised in HARDWARE_REPORT.md |
| `TC-HW-02` (display colour bars, grid, orientation) | `PARTIAL` | test pattern drawn and orientation confirmed by the operator; golden screenshot comparison NOT RUN |
| `TC-HW-03` (touch accuracy grid after calibration) | `NOT RUN` | needs calibration (GATE-04) |
| `TC-HW-04` (uncalibrated boot safety) | `NOT RUN` | needs the calibration module |
| `TC-PR-01..14` (protocol codec) | `NOT RUN` | codec is **generated** and its caps verified by inspection, but no fixture vector has been encoded or decoded yet (M2) |
| all other `TC-*`, `UX-*` | `NOT RUN` | their milestones have not started |

Evidence directories are named `docs/evidence/<yyyymmdd>/<TC-ID>/` (§19.1). The M1
captures live under `docs/evidence/20261016/`.

---

## Tooling verification

Tools are only useful if they fail when they should, so each was run against a real
defect rather than only against passing input:

| Tool | Verified how | Result |
|---|---|---|
| `tools/setup_check.py` | ran on this host; caught a stale partition path, a deprecated PlatformIO command and an unborn HEAD | fixed, now `PASS` |
| `tools/contrast_check.py` | caught 6 of 7 hand-converted RGB565 constants being wrong | fixed, now `PASS` (16 pairs) |
| `tools/check_dependency_pins.py` | initially mis-parsed the platform pin and failed | fixed, now `OK` |
| `tools/test-only/serial_harness/capture.py` | crashed on a Windows console encoding issue mid-capture | fixed; it now also exits non-zero if a `FAIL` line appears |
| `tools/gen_lv_conf.py` | generates the LVGL config; fails if the template loses a required setting | used to generate `firmware/include/lv_conf.h` |
| `tools/gen_proto.py` | its own `--check` mode caught it reporting OK while writing nothing (the write path generated into a temporary directory that was then deleted) | fixed; now writes `firmware/src/protocol/generated/` and drift-checks it |
| `protocol/dashcue.options` | run through the real nanopb generator | **rejected three lines** (`type:FT_FLOAT` is not a nanopb value), removed, with the wire-form guarantee explained |

---

## 2026-10-17, HUD three-tab overhaul and companion UI wiring

This entry covers the change set evidenced in
[docs/evidence/20261017/](evidence/20261017/README.md). It does not replace the
2026-10-16 hardware entries; those remain the only physical measurements.

| Check | Command | Result | Evidence |
|---|---|---|---|
| GATE-01 re-run | `python tools/setup_check.py` | `PASS` with one warning: no serial port (CYD not attached) | [gate01_setup_check.log](evidence/20261017/gate01_setup_check.log) |
| Native firmware suite | `pio test -e native -d firmware` | `PASS`, 22/22 | [gate02_native_tests.log](evidence/20261017/gate02_native_tests.log) |
| Firmware build | `pio run -e cyd_ili9341 -d firmware` | `PASS`; flash 55.0% | [gate03_firmware_build.log](evidence/20261017/gate03_firmware_build.log) |
| Static DRAM budget | `python tools/memory_report.py …` | `PASS`: **69,364 B** ≤ 70,000 B | [gate03_memory_report.json](evidence/20261017/gate03_memory_report.json) |
| Android unit suites | `./gradlew testReleaseUnitTest` | `PASS`, 71 tests / 0 failures | [gate04_android_unit_tests.log](evidence/20261017/gate04_android_unit_tests.log) |
| Android release | `./gradlew :app:assembleRelease` | `PASS`, 52,166,508 B APK | [gate05_android_release.log](evidence/20261017/gate05_android_release.log) |
| Host model E2E | `python -m pytest tests/e2e/test_tier1..4` | `PASS`, 265 cases (models only) | [gate06_e2e_host_model.log](evidence/20261017/gate06_e2e_host_model.log) |
| Phone install | `adb install -r …` | `PASS`, Success on CPH2585IN | [adb_install.log](evidence/20261017/adb_install.log) |
| Phone launch | `adb shell am start …` | process alive, no `AndroidRuntime` crash | [device_state.txt](evidence/20261017/device_state.txt) |

### Still blocked after this change

* **GATE-03 LVGL runtime, GATE-04 touch calibration, GATE-05 BLE on hardware:** not
  re-run. The board is not on a serial port in this session, so the new 69,364 B
  static number is a build artifact measurement, **not** a runtime heap measurement.
* **Phone visual sign-off:** the handset is awake but `deviceLocked=1` and the
  focused window is the notification shade. The app launches behind the keyguard;
  no four-tab screenshot exists in this evidence set.
* **GATE-10 routing/geocoding:** still `BLOCKED`, no OSRM/Nominatim endpoint was
  supplied, and DEC-14 forbids shipping a default.
* **Artwork:** no firmware ARTWORK_BEGIN/CHUNK consumer exists; the HUD renders the
  vector fallback glyph only.
