# Test plan

## Layered checks

| Layer | Purpose | Command / evidence | Limits |
|---|---|---|---|
| Android unit, lint and packaging | Kotlin modules, static lint, APK assembly | `cd android && ./gradlew testDebugUnitTest lintDebug :app:assembleDebug :app:assembleRelease` | Does not install, grant permissions, pair or verify hardware |
| Firmware native | Shared hardware-independent protocol/state/touch sources | `cd firmware && pio test -e native -v` | Host native runtime, not FreeRTOS/ESP32 peripherals |
| Firmware target build | Product target compilation/link | `cd firmware && pio run -e cyd_ili9341` | Does not flash or prove selected panel/audio compatibility |
| Protocol fixtures | Cross-language golden bytes and generator drift | `python protocol/generate_fixtures.py`; `python tools/gen_proto.py --check` | Requires explanation/review of expected fixture changes |
| Host E2E model | Simulated protocol workflows/source assertions | `python tests/e2e/run_e2e_tests.py --tier all -v` | Explicitly not Android, radio, phone, or physical HUD E2E |
| Hardware audit | Serial boot/heap and ADB inventory | `python tools/run_hardware_checks.py --port COM4 --seconds 25` | Resets/captures only; never proves visible panel or workflow |

## Required cases

- Permissions granted, denied, revoked; Bluetooth off/on; scan/connect/service discovery/CCCD/HELLO/READY.
- MTU 23 and 247; malformed/oversized frames, CRC mismatch, invalid fragments, out-of-order/duplicate chunks, timeouts and reconnect during transfer.
- Media session changes, missing metadata, command capability handling and confirmed command outcomes.
- Navigation notification parsing, endpoint failure, route changes, location loss and stale-state clearing.
- Settings updates and round-trip persistence; app background/foreground and process recreation.
- Firmware queue saturation, invalid settings/NVS, repeated transfer and UI/touch behavior under BLE load.
- Security: authenticated fresh bond, reject unauthenticated characteristic access; ensure logs contain no passkey.

Unsupported/unavailable cases remain NOT RUN or BLOCKED rather than simulated PASS. For each scenario preserve command, build identity, environment and artifact. Test-only Python models must be labeled as model tests.

## Release decision

A release candidate needs passing source/build checks and a separate physical handset-to-HUD workflow on the exact board/panel profile, including security pairing, disconnect/reconnect, media/settings and visual inspection. Current blockers are recorded in [E2E results](e2e-results.md) and [hardware validation](hardware-validation.md).