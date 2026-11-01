# Changelog

All notable changes to this project are documented in this file.

## [Unreleased]

### Removed

- BLE pairing requirement (BUG-04, product decision): bonding, MITM/LE Secure
  Connections, the six-digit passkey and its GAP handler, the pairing window and
  countdown, the `ui_screen_pairing.{h,cpp}` screen, `UI_SCREEN_PAIRING`, the
  passkey/dismiss IPC events, `IPC_CMD_PAIR_PHONE`, `IPC_CMD_CANCEL_PAIRING`,
  the `DASHCUE_NO_PAIRING` build flag, the HUD Settings **Pair** button, the
  navigation screen's "Start pairing" action, and the Android Companion Device
  Manager association flow (`CompanionDeviceHelper.kt`). RX/TX/CCCD are now
  unauthenticated and the HUD advertises continuously; the open link is
  documented as an accepted risk in the threat model.
- `ble_peripheral_tick()`, `app_state_*_pairing_window()` and the passkey state,
  including the per-loop countdown bug they carried.
- Dead `m_bonded` UI lifecycle from the HUD; the BLE peripheral no longer exposes
  or consumes bonded state for the connection path.
- Artwork transport over the protocol (previous refactor), reclaiming its static
  DRAM.

### Changed

- The BLE link is deliberately unauthenticated. The protocol layer (framing
  bounds, CRC32, protobuf field validation, dedupe, fragmentation) is the only
  validation boundary; see [threat model](security/threat-model.md) and
  [bug audit](testing/bug-audit.md) BUG-04.
- Android connection setup timeout reduced from 120 s to 20 s: there is no
  passkey step to wait for, so a stalled connect is treated as a failure.
- Firmware board definition moved to `boards/cyd_3248s035r.json` (CYD
  ESP32-3248S035R, 3.5-inch 480x320 ST7796); the `[env]` default board is now
  `cyd_3248s035r` instead of the historical `cyd_2432s028r`.
- Android dependency pins realigned with the toolchain: `core-ktx` 1.17.0,
  `lifecycle-*` 2.10.0, `navigation-compose` 2.9.5. Previous pins required
  compileSdk 37 and AGP 9.1, which masked an AAR-metadata failure by disabling
  the checks; the checks are re-enabled and the pins are compatible
  ([DEPENDENCIES.md](DEPENDENCIES.md)).

### Fixed

- Confirmation dialog on the navigation screen no longer passes taps through to
  the controls underneath it (the scrim now sets `LV_OBJ_FLAG_CLICKABLE`, which
  LVGL's hit test requires).
- UI audit Rule 3 no longer demands both `LV_EVENT_CLICKED` and
  `LV_EVENT_SHORT_CLICKED`, which fired one tap twice; it now rejects that
  combination. Rule 2 no longer flags a deliberate `ext_click_area(0)`.
- Native firmware tests run on Linux CI: the Unity test runner is invoked
  through a cross-platform Python wrapper (`firmware/run_native_test.py`) instead
  of a PowerShell-only script.
- The host-simulation suite no longer hard-fails when the cross-compiled ESP32
  ELF is absent; the static-DRAM budget gate stays authoritative in the Firmware
  CI compilation job, and the adversarial memory test runs whenever the ELF is
  available.
- Android `lintDebug`: `AarMetadata` check tasks are no longer blanket-disabled
  in the root build file, resolving the `bundleDebugLocalLintAar`
  "input file does not exist" CI failure.

### Added

- `tests/adversarial/test_touch_hitbox_geometry.py`: deterministic, hardware-free
  hitbox model (half-open bounds, first-hit-wins child search, modal occlusion,
  disabled controls, shared boundaries) with a source anti-drift guard.
- `docs/testing/bug-audit.md`: reproducible defect log.
- `StaleAssociationCleaner` on Android: removes legacy bonds for the HUD left
  behind by the previous pairing flow.
