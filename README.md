# DashCue — Android companion and ESP32 HUD

DashCue is a work-in-progress phone companion and embedded heads-up display. Android Kotlin modules handle BLE, media sessions, settings and navigation adapters; ESP32 firmware uses PlatformIO/Arduino, Bluedroid BLE, bounded protocol/state queues, LVGL and board-specific hardware drivers.

> **Not release-ready.** The 2026-10-28 physical capture reports an I2S driver-registration error; the Android package was absent on the inspected handset; authenticated pairing and phone↔HUD data flows were not run; and the captured HUD reports `cyd-3248s035r-st7796`. Confirm the intended physical board/panel before flashing anything. This is not automotive-safety-certified equipment.

## Actual status

| Check | Status | Scope |
|---|---|---|
| Firmware native tests | PASS (baseline 24/24) | Host tests, not ESP32 peripheral execution |
| Firmware product build | PASS (baseline) | Compilation only; no matching-profile flash claim |
| Android unit tests | PASS (baseline 138 tests) | Unit tests, not physical BLE |
| Python all-tier host suite | PASS (prior 265 cases) | Models/source checks; predates latest regression assertion |
| Physical boot | PASS, 2,125 ms | One capture; board profile still needs confirmation |
| I2S registration | FAIL | Serial error; audio not verified |
| Android app installed | FAIL | Package lookup found no `com.dashcue.app` |
| Physical phone↔HUD E2E | NOT RUN | No paired session evidence |

Current evidence and limits: [E2E results](docs/testing/e2e-results.md), [hardware validation](docs/testing/hardware-validation.md), and [audit findings](docs/audit/findings.md).

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
- Firmware configures authenticated encrypted BLE attributes and Secure Connections + MITM; fresh physical pairing and unauthorized-access rejection still need verification.
- Android's artwork transmitter is disabled; album art transfer is not enabled end-to-end.
- Navigation requires location permission and user-configured routing/geocoding endpoints.
- Optional A2DP/audio must not be considered working until the captured I2S registration failure is diagnosed and sound is observed.

## Documentation

- [Architecture overview](docs/architecture/system-overview.md) and [component map](docs/architecture/component-map.md)
- [Protocol contract](docs/protocol/protocol-specification.md) and [shared test vectors](docs/protocol/test-vectors.md)
- [Test plan](docs/testing/test-plan.md), [results](docs/testing/e2e-results.md), and [hardware record](docs/testing/hardware-validation.md)
- [Security threat model](docs/security/threat-model.md), [audit baseline](docs/audit/baseline.md), and [findings](docs/audit/findings.md)
- [Performance baseline](docs/performance/baseline.md) and [results](docs/performance/results.md)
- [Release checklist](docs/release/release-checklist.md)
- Six Mermaid sources under [docs/diagrams](docs/diagrams/)

See [setup](docs/SETUP.md), [hardware report](docs/HARDWARE_REPORT.md), and [protocol source](protocol/dashcue.proto) for more detail. Release readiness remains **NOT READY** until all blockers in the checklist have real evidence.