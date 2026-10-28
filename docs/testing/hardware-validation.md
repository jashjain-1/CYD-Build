# Hardware validation record — 2026-10-28

## Scope and observed environment

This is a physical bring-up observation, not application-to-HUD E2E. A physical OnePlus CPH2585 (Android 16/API 36) and COM4 serial device were visible. The handset did not have package `com.dashcue.app` installed. The serial firmware banner identifies `hw=cyd-3248s035r-st7796` and a 480×320 display. Compare that captured profile with the product build/board definition before any flash; no mismatched image was flashed.

## Results

| Observation | Status | Evidence |
|---|---|---|
| Boot reached UI | PASS | `boot_to_ui_ms=2125`, no `BOOT FAILED` line |
| Captured heap sample | PASS (single run) | Free 54,188 B, minimum 51,272 B, largest block 32,756 B at a reported stats sample |
| Stability window | NOT RUN | Report counted 15 s observed stats span vs required 20 s, despite later serial lines; do not override captured report classification |
| Display colors / panel match | NOT RUN | Serial telemetry cannot establish visible image or panel controller |
| Touch accuracy | NOT RUN | Requires five real presses and visible coordinate/error evidence |
| I2S/audio init | FAIL | `E (1934) I2S: register I2S object to platform failed`; subsequent task message does not prove driver registration |
| Android app installed | FAIL | Package lookup says `Unable to find package: com.dashcue.app` |
| BLE pairing and data path | NOT RUN | No app package/session evidence |
| Media/navigation/settings, reconnect, soak, driving visibility | NOT RUN | Separate physical scenarios absent |

Raw files: [JSON report](../evidence/20261028/hardware-audit-run/report.json), [HUD serial](../evidence/20261028/hardware-audit-run/hud-serial.log), [capture command transcript](../evidence/20261028/hardware-audit-run/capture-command.log), and [phone package query](../evidence/20261028/hardware-audit-run/phone-package.log). An earlier non-reset serial sample is [audit-physical-serial.log](../evidence/20261028/audit-physical-serial.log); it does not demonstrate the current build or full workflow.

## Safe next procedure

1. Identify the exact board/panel and compare it to `firmware/platformio.ini` and `firmware/include/dashcue_config.h`; do not flash until they match.
2. Diagnose I2S driver ownership/registration with the selected ESP32 core and A2DP configuration, then rerun a build and serial test. Do not report the audio task's log as success.
3. Install a reviewed APK on the authorized handset, complete Android permission setup, and make a fresh authenticated pairing. Check HELLO/ACK and settings/media transfer from both ends.
4. Observe the physical screen, touch controls, disconnect/reconnect and audio output. Capture serial + ADB evidence; avoid collecting passkeys, destinations, or media content.
5. Run repeated reconnections and soak only after basic stability; record duration and all resets.
