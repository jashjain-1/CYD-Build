# Evidence — 2026-10-17

Change set: HUD three-tab overhaul (schematic map removed) and the companion app's
four-tab UI wired to real media, navigation, settings and diagnostics paths.

## Commands and results

| Gate | Command | Exit | Evidence |
|---|---|---|---|
| GATE-01 host toolchain | `python tools/setup_check.py` | 0 | [gate01_setup_check.log](gate01_setup_check.log) — PASS with `serial_ports` warning (no CYD attached) |
| Firmware native tests | `pio test -e native -d firmware` | 0 | [gate02_native_tests.log](gate02_native_tests.log) — 22/22 |
| Firmware build | `pio run -e cyd_ili9341 -d firmware` | 0 | [gate03_firmware_build.log](gate03_firmware_build.log) |
| Static DRAM | `python tools/memory_report.py firmware/.pio/build/cyd_ili9341/firmware.elf` | 0 | [gate03_memory_report.json](gate03_memory_report.json) — **69,364 B**, limit 70,000; ELF sha256 `a0ffdaa8…c376f` |
| Android unit tests | `./gradlew testReleaseUnitTest` | 0 | [gate04_android_unit_tests.log](gate04_android_unit_tests.log) — 71 tests, 0 failures |
| Android release APK | `./gradlew :app:assembleRelease` | 0 | [gate05_android_release.log](gate05_android_release.log) — 52,166,508 B, `app/build/outputs/apk/release/app-release.apk` |
| Host model E2E suite | `python -m pytest tests/e2e/test_tier1_features.py … test_tier4_scenarios.py` | 0 | [gate06_e2e_host_model.log](gate06_e2e_host_model.log) — 265 passed |
| Phone install | `adb install -r android/app/build/outputs/apk/release/app-release.apk` | 0 | [adb_install.log](adb_install.log) — Success |
| Phone launch | `adb shell am start -n com.dashcue.app/.MainActivity` | 0 | [adb_start.log](adb_start.log); process alive in [device_state.txt](device_state.txt) |
| GATE-08 pins | `python tools/check_dependency_pins.py` | 0 | [gate08_dependency_pins.log](gate08_dependency_pins.log) — `RESULT: OK` |

## What changed, in one line each

* HUD tab bar is **Media / Nav / More** (three 80×48 targets). The schematic map
  screen and its 1 KB route snapshot were deleted, so the HUD only renders
  glanceable cue cards; the map lives on the phone.
* The companion app now has **Dashboard / Navigation / Settings / Diagnostics**
  tabs backed by `DashCueViewModel`; it mirrors MediaState/NavState/SettingsSet
  to the HUD and executes inbound MediaCmd/NavCmd with real `CmdResult` replies.
* Navigation uses the pinned MapLibre 13.7.1 view with a local raster style,
  user-configured OSRM/Nominatim endpoints, Room-backed favorites and recents.
* Diagnostics shows negotiated MTU/RSSI/frame counters, HUD DeviceStatus and a
  JSON export bundle.

## Blockers and explicit non-claims

* **Physical HUD: BLOCKED.** `setup_check` reports no serial port; COM4 is not
  present in this session. No flash, boot-time, heap or touch measurement is claimed.
* **Phone UI visual check: BLOCKED.** The handset is awake but has a secure
  keyguard (`deviceLocked=1`, `mCurrentFocus=NotificationShade`). The app process
  starts and holds a surface, but no screenshot of the four tabs exists, so this
  evidence set makes **no visual-pass claim**.
* **Map tiles / routing: NOT RUN.** No OSRM or Nominatim endpoint is configured
  (DEC-14 forbids a baked-in default), so search, route preview and turn guidance
  remain unverified at runtime.
* **Artwork streaming: NOT IMPLEMENTED.** The firmware has no ARTWORK_BEGIN/CHUNK
  consumer; the HUD keeps its vector fallback glyph. The 64×64 artwork path is
  therefore a documented gap, not a pass.
* **Host model E2E is not hardware E2E.** The 265 passing cases exercise Python
  models under `tools/test-only/`, not the phone and HUD together.
