# Audit findings and remediation ledger

Audit date: 2026-10-28. Confidence reflects source/toolchain evidence, not a full radio penetration test.

| ID | Class / severity | Confidence | Finding and evidence | Remediation / status |
|---|---|---|---|---|
| SEC-01 | Security, High | Confirmed | `firmware/src/transport/ble_peripheral.cpp` configured `ESP_LE_AUTH_BOND` + `ESP_IO_CAP_NONE` (Just Works) and applied `ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE` to both data characteristics. That did not enforce the documented LE Secure Connections + MITM policy. The pinned SDK defines `ESP_LE_AUTH_REQ_SC_MITM_BOND`, `ESP_IO_CAP_OUT`, and `ESP_GATT_PERM_*_ENC_MITM`. | Fixed in source: require SC+MITM+bond, DisplayOnly capability, authenticated/encrypted RX write, TX read, and CCCD read/write. The stack-generated passkey remains displayed by the HUD. Regression source assertion added in `tests/e2e/test_tier1_features.py`. Needs physical pairing + unauthorized GATT attempt retest; no radio pass is claimed. Existing paired clients may need to forget/re-pair if their stored bond is legacy. |
| SEC-02 | Security/privacy, Low | Confirmed | The pairing code printed the live six-digit Bluetooth passkey to serial (`PASSKEY NOTIFICATION passkey=...`), which can leak through captured UART logs. | Fixed: serial logs only state that the passkey notification arrived; code remains in UI state for on-screen display. Regression checked by source review; future test may assert no passkey interpolation in serial calls. |
| REL-01 | Compatibility/reliability, High | Confirmed | Firmware advertises a 2,048-byte payload cap, but Android `DashCueFraming` accepted/encoded 4,096 bytes. A phone could report a 4,096 cap in HELLO and fail to send frames that the HUD rejects. | Fixed Android frame and reassembly bounds at 2,048; HELLO uses the same constant. Added 2,048-byte frame roundtrip and MTU-23/247 fragmentation boundary coverage. Requires cross-device E2E to close. |
| DOC-01 | Maintainability, Medium | Confirmed | `docs/PROTOCOL.md` and `protocol/uuids.md` described different envelope/fragment fields, credits and security than source; some claims were aspirational or stale. | Corrected protocol/UUID documentation and added source-grounded diagrams and test instructions. Outstanding feature gaps are marked unverified, not implemented. |
| BUILD-01 | Build hygiene, Medium | Confirmed | `python tools/gen_lv_conf.py --check` failed because generated `firmware/include/lv_conf.h` drifted from the pinned LVGL template. | Regenerated from local pinned template; rerun generator check and product build. |
| AND-01 | Android lint, Low | Confirmed | `lintDebug` reported two MissingPermission errors on `startObservingDevicePresence` and `stopObservingDevicePresence`; the exact manifest permission was absent at baseline. | Added `REQUEST_OBSERVE_COMPANION_DEVICE_PRESENCE` and optional `android.software.companion_device_setup` feature declaration. API docs identify association separately; no runtime permission flow is presumed. Re-run lint. |

## Architecture decision record

- **Keep the existing architecture:** Android application/UI and feature modules → `:core:ble` GATT central → `:core:protocol` framing/protobuf → BLE GATT → ESP32 Bluedroid peripheral → firmware reassembly/dispatch (`proto_task`) → FreeRTOS queues/state → LVGL display/touch/audio HAL. No large refactor was justified by the confirmed defects.
- **Restore the documented BLE access policy:** use the pinned Bluedroid security enums and attribute permissions rather than changing BLE stack or UUID. Compatibility cost: existing Just Works bonds may need explicit forget/re-pair; security improves at the actual GATT enforcement point.
- **One payload ceiling:** set Android to the firmware's already advertised 2,048-byte cap rather than raising firmware's statically allocated reassembly buffers. This avoids more static RAM and makes both endpoints reject/accept the same payload size.
- **Generated LVGL config:** regenerate output from its pinned template rather than hand-edit generated content.

## Remaining issues / accepted boundaries

- Hardware retest of authenticated pairing, GATT attribute access, reconnect, media and navigation is required after the security policy change. It is not proven by code compilation or host tests.
- Existing app behavior may not actively update Android's `DashCueFragmentation` credit pool from Heartbeat payloads (the write path sends acknowledged writes one fragment at a time); the code has a credit field but no end-to-end flow-control test. Do not claim the protocol's intended credit window is implemented end-to-end until verified/fixed.
- `sendMessage` serializes every GATT write and awaits the platform callback. This is a deliberate reliable write path; no performance gain has been measured.
- Baseline `python tools/check_dependency_pins.py` exited 1; diagnose and record the actual result in current verification before release.
- The attached device list and COM4 enumeration do not prove the current app build is installed, the HUD is paired, or the board's touch overlay works. See hardware report/runbook.
