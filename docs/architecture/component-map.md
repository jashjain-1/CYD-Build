# Component map

## Android modules

| Component | Responsibility | Main boundary / caveat |
|---|---|---|
| `:app` | Activity, Compose UI, ViewModel, app-level wiring | `DashCueSyncBridge` connects application state to BLE and feature flows |
| `:core:ble` | Scan/connect, GATT operations, readiness state, retries and telemetry | Serializes acknowledged characteristic writes; depends on Android Bluetooth APIs |
| `:core:protocol` | Envelope/CRC and fragment encode/decode/reassembly | Uses generated protobuf types; 2,048-byte payload ceiling |
| `:core:common` | Shared logging and common types | Logging must not include pairing secrets or private destinations |
| `:core:database` | Room-backed local data | No server is present in this project |
| `:feature:media` | Active media sessions, metadata, command dispatch and artwork lookup | Artwork RGB565 transfer is disabled in the app bridge |
| `:feature:navigation` | OSRM routing, Nominatim-compatible geocoding, location calculations and notification parsing | Requires user setup, permissions and external endpoints for route calculation |
| `:feature:onboarding`, `:feature:settings`, `:feature:diagnostics` | Permission/setup, preferences and diagnostics UI | Runtime behavior depends on Android version and grants |

## Firmware subsystems

| Subsystem | Responsibility | Boundary / resource constraint |
|---|---|---|
| `board/` and `display/` HALs | Board startup, TFT/LVGL display, backlight | Board profile/pin map is revision-specific |
| `touch/` | Touch controller polling, calibration and events | Dedicated FreeRTOS task; physical touch calibration is hardware-specific |
| `transport/ble_peripheral.cpp` | Bluedroid GATT peripheral, pairing and characteristic callbacks | Callback copies bounded fragments into RX queue; Secure Connections + MITM policy configured in source |
| `protocol/framing.*`, `fragmentation.*` | Frame validation, CRC32, reassembly and credit helper | Fixed-size bounded frame buffers, two lanes, 2-second reassembly timeout |
| `transport/proto_task.*` | Protocol task, HELLO/snapshot, message dispatch and outbound notifications | Owns protocol processing; interacts with other tasks through IPC queues |
| `state/` | Device/application state and FreeRTOS IPC messages/queues | Queue capacity is bounded; drops are counted |
| `ui/` | LVGL screens, status/navigation/media/settings UI and artwork store | Artwork store exists but transmitter is disabled on Android |
| `audio/` and A2DP sink | Tone output and optional Classic Bluetooth A2DP | I2S initialization error observed in the 2026-10-28 serial capture |

## Build and test support

- Android: `android/` Gradle multi-module project, wrapper pins Gradle 8.14.6.
- Firmware: `firmware/platformio.ini`; product, diagnostic and native test environments.
- Protocol schema: `protocol/dashcue.proto`; firmware nanopb generator and Android Gradle protobuf generation.
- Verification scripts and test-only simulators: `tools/`, `tests/e2e/`, `tests/adversarial/`.

Physical availability is not a test result. Current capture, phone install status, and profile caveat are in [hardware validation](../testing/hardware-validation.md).