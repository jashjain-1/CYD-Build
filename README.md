# DashCue — Android companion and ESP32 HUD

DashCue is a work-in-progress phone companion and embedded heads-up display. Android Kotlin modules handle BLE, media sessions, settings and navigation adapters; ESP32 firmware uses PlatformIO/Arduino, Bluedroid BLE, bounded protocol/state queues, LVGL and board-specific hardware drivers.

> **Not release-ready.** The captured board identifies as `cyd-3248s035r-st7796`, but historical board definitions and measured reports conflict with parts of the current display/touch pin map. Confirm your exact board revision before flashing; audio and complete phone↔HUD behavior also remain incompletely verified. This is not automotive-safety-certified equipment.

## Actual status

| Check | Status | Scope |
|---|---|---|
| Firmware native tests | PASS (baseline 24/24) | Host tests, not ESP32 peripheral execution |
| Firmware product build | PASS (baseline) | Compilation only; no matching-profile flash claim |
| Android unit tests | PASS (baseline 138 tests) | Unit tests, not physical BLE |
| Python all-tier host suite | PASS (prior 265 cases) | Models/source checks; predates latest regression assertion |
| Physical boot | PASS, 2,125 ms | One capture; exact board profile and display/touch map need reconciliation |
| I2S registration | FAIL in prior capture | Separate later A2DP state events are not proof that I2S initialization succeeded |
| Android app installed | Historic absence; newer session artifacts show an install | Inspect current evidence before claiming an app/hardware pass |
| Physical phone↔HUD E2E | PARTIAL / NOT fully evidenced | Prior user report and phone captures do not prove current-build HUD pixels, touch matrix, or sustained audio |

Current evidence and limits: [E2E results](docs/testing/e2e-results.md), [hardware validation](docs/testing/hardware-validation.md), and [audit findings](docs/audit/findings.md).

## Plug-and-play setup

Start with the [setup guide](docs/SETUP.md). Check the board before flashing: the
current firmware profile is for a **480×320 ST7796 CYD**; historical 2.8-inch
ESP32-2432S028R reports describe different display/touch wiring. Do not flash the
current profile to a different revision without verifying its panel and pin map.

Quick path for matching hardware (replace `COM4` with the port shown by
`pio device list`):

```bash
python tools/setup_check.py
cd firmware && pio run -e cyd_ili9341
pio run -e cyd_ili9341 -t upload --upload-port COM4
cd ../android
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.dashcue.app/.MainActivity
```

Grant Bluetooth permissions when prompted, then tap **Connect** in the app; BLE
GATT does not require pairing. Media mirroring additionally needs Android
Notification access. Navigation needs location permission and user-configured
routing/geocoding service endpoints; see [routing setup](docs/ROUTING_SETUP.md).
Classic Bluetooth A2DP audio is a separate capability. These setup steps do not
prove screen/touch/audio correctness on an unverified board revision.

## Build and test

```bash
# Android
cd android
./gradlew testDebugUnitTest lintDebug :app:assembleDebug :app:assembleRelease

# Firmware host tests and target build
cd ../firmware
pio test -e native -v
pio run -e cyd_ili9341

# From repository root: schema/generator and host-model checks
cd ..
python tools/gen_proto.py --check
python protocol/generate_fixtures.py
python tests/e2e/run_e2e_tests.py --tier all -v
```

These commands verify software builds/tests only. They do not install the app, pair BLE, flash hardware, or certify visible/audio behavior. Confirm the selected firmware board profile before any flash.

## Feature caveats

- The BLE frame uses a 16-byte header, a protobuf payload (maximum 2,048 bytes), and a 4-byte IEEE CRC32 trailer. CRC is not authentication.
- BLE GATT is unauthenticated and has no pairing/passkey step. Nearby devices can attempt connections; CRC is not authentication. Read the [threat model](docs/security/threat-model.md).
- Android's artwork transmitter is disabled; album art transfer is not enabled end-to-end.
- Navigation requires location permission and user-configured routing/geocoding endpoints.
- A2DP is a separate Classic Bluetooth audio path; diagnose the captured I2S registration error and verify sound on the exact image before treating it as reliable.

## Documentation

- [Architecture overview](docs/architecture/system-overview.md) and [component map](docs/architecture/component-map.md)
- [Protocol contract](docs/protocol/protocol-specification.md) and [shared test vectors](docs/protocol/test-vectors.md)
- [Test plan](docs/testing/test-plan.md), [results](docs/testing/e2e-results.md), and [hardware record](docs/testing/hardware-validation.md)
- [Security threat model](docs/security/threat-model.md), [audit baseline](docs/audit/baseline.md), and [findings](docs/audit/findings.md)
- [Performance baseline](docs/performance/baseline.md) and [results](docs/performance/results.md)
- [Release checklist](docs/release/release-checklist.md)
- Seven Mermaid sources under [docs/diagrams](docs/diagrams/)

See [setup](docs/SETUP.md), [hardware report](docs/HARDWARE_REPORT.md), and [protocol source](protocol/dashcue.proto) for more detail. Release readiness remains **NOT READY** until all blockers in the checklist have real evidence.