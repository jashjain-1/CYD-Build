# E2E results — 2026-10-29 (updated after live handset + CYD check)

Environment: a personal Android handset over ADB (serial removed for privacy) and the user's physical CYD
connected on COM4 during the live session. The current board profile was
identified from serial as `cyd-3248s035r-st7796`, 480x320. The user confirms
"map and all is working" and that the board produces sound. The latest app
screenshot shows Google Maps navigation active on the handset. Evidence captured
during the session establishes phone-side Maps UI and BLE/media state; the exact
HUD pixels and touch calibration were not captured. Do not treat those as a
visual/touch pass.

## What was actually run

| Step | Command | Result |
|---|---|---|
| Firmware product build | `cd firmware && pio run -e cyd_ili9341` | **SUCCESS** — RAM 18.3 % (59 936 B), Flash 56.2 % (1 767 089 B) |
| Firmware host tests | `cd firmware && pio test -e native` | **PASSED** — 24/24 cases |
| UI static audit | `python tools/audit_ui_screens.py` | **PASS** — 6/6 screens, exit 0 |
| Focused accessibility/touch pytest | `python -m pytest tests/adversarial/test_touch_hitbox_geometry.py tests/adversarial/test_a11y_contrast_adversarial.py tests/adversarial/test_adversarial_bare_finger_cockpit_a11y.py -q` | **FAIL** — 32 passed, 2 failed: legacy layout assertions expect media ext-click 10 and a removed offline-pair button |
| Adversarial models | `python -m pytest tests/adversarial -q` | **FAIL** — most recent focused run: 2 failures in stale touch-layout assertions (media ext-click and removed pairing control), 32 passed; do not claim full suite PASS |
| App compile | `cd android && ./gradlew :app:compileDebugKotlin` | **SUCCESS** |
| App package | `cd android && ./gradlew :app:assembleDebug` | **SUCCESS** — `app-debug.apk` |
| App install | `adb install -r app-debug.apk` | **Success** |
| App launch | `adb shell am start -n com.dashcue.app/.MainActivity` | Running, `mCurrentFocus` = MainActivity |

## Phone-side observations (real handset, genuine screenshots)

¹ Phone screenshots carry personal location/device data and are kept **local
only** (`.gitignore` excludes `docs/evidence/**/phone-*.png`); the table records
what they showed. The committed evidence form for device behaviour is the
redacted serial capture.
| # | Screenshot | Observation | Verdict |
|---|---|---|---|
| 1 | `docs/evidence/20261029/phone-01-launch.png`¹ | First-run permission setup renders correctly | PASS |
| 2 | `docs/evidence/20261029/phone-04-dashboard.png` | **Defect:** title/clock drawn under the system status bar; the "Disconnect" button wrapped one character per line | FAIL → fixed (BUG-09) |
| 3 | `docs/evidence/20261029/phone-06-settings.png` | **Defect:** Android showed "Enter pairing code — Pair with DashCue-HUD" while the app scanned | FAIL → see BUG-08 |
| 4 | `docs/evidence/20261029/phone-05-dashboard-fixed.png` | After fix: title, clock and status pill below the status bar; Connect / Refresh / Disconnect on one line each; tab bar clear of the gesture area | PASS |
| 5 | `docs/evidence/20261029/phone-08-no-pairing-prompt.png` | Pairing dialog still returns on this handset | Blocked on a user action, see BUG-08 |

## Pairing-code prompt: what it is and what was done

`adb shell dumpsys bluetooth_manager` showed:

```
XX:XX:XX:XX:B7:C2(Public ) => XX:XX:XX:XX:XX:XX(Unknown) [ DUAL ] ... DashCue-HUD
    [Packages    ]: [com.dashcue.app]
```

That is a **Companion Device Manager association created by the previous
pairing build**, with a null/unknown address, still owned by `com.dashcue.app`.
Android keeps trying to finish it and shows a legacy PIN dialog, even though both
the current firmware and the current app have no pairing step.

Actions taken in code:

1. The CDM association flow (`CompanionDeviceHelper`, `requestCdmPairing`) is
   deleted, so no new association is ever created.
2. `StaleAssociationCleaner.clean()` runs at app start and on "Forget HUD" and
   disassociates any association that has a usable MAC.
3. Because the record on this handset has a **null address**, the public API
   (`disassociate(String)`) cannot remove it. Android exposes no public way for an
   app to delete a system bond, so Settings now shows an explicit instruction
   when a `DashCue` bond exists in system Bluetooth settings.
4. The current firmware removes BLE pairing for the GATT data link. The
   Classic Bluetooth A2DP sink keeps the library's Secure Simple Pairing
   configuration and expects the normal operating-system confirmation for an
   initial audio bond (`firmware/src/main.cpp`). The earlier pairing prompt was
   not conclusively attributed to BLE versus Classic Bluetooth; no fixed `0000`
   PIN is configured. See the threat model for the intentionally unauthenticated
   BLE GATT trust boundary.

**Remaining user action on an already-paired phone:** remove the old
`DashCue-HUD` entry in Settings → Bluetooth once. After that the app connects
with no code. This could not be done from ADB on this handset (the OEM pairing
dialog is owned by `com.oplus.wirelesssettings` and no `unpair` shell command
exists in `cmd bluetooth_manager`).

## Live Maps / music check — limited hardware evidence

| Test | Objective and steps | Observed result | Status / evidence |
|---|---|---|---|
| HW-MAP-01 | With the phone connected to the CYD, run an active Google Maps route and check navigation updates | User confirms Maps and the system are working; live Maps screen was captured on the physical phone. Earlier ADB observation recorded Maps navigation state sent by the Android bridge over BLE. The current CYD display contents were not photographed or serial-dumped. | **PARTIAL PASS** — phone and user-reported end-to-end operation; HUD visual output unrecorded. [Phone screenshot](../evidence/20261029/phone-11-maps-live.png) |
| HW-AUD-01 | Start phone music while connected, check A2DP state and hear the HUD speaker | User confirms board produces sound; earlier ADB evidence reported the DashCue A2DP profile connected/playing. No audio recording, controlled test track, or verified current flashed-image identity was saved. | **PARTIAL PASS** — physical audible output confirmed by user; exact test build/track unrecorded. |
| HW-FW-01 | Identify board and observe boot/connectivity | Serial during the session identified `cyd-3248s035r-st7796`, 480x320, and COM4 was used for the device. Serial open resets this board, so no further session was opened after the user said operation was good. | **OBSERVED** — not a full boot/soak record. |

### Follow-up hardware check — 2026-10-29

A later request asked to demonstrate Maps and music on hardware. At this check,
`adb devices -l` listed no handset; `pio device list` found the CYD serial adapter
on COM4. To preserve the running board state, I captured serial output with the
harness's `--no-reset` option (no flash, reset, or Bluetooth state change):

`python tools/test-only/serial_harness/capture.py --port COM4 --seconds 30 --no-reset --out docs/evidence/20261029/hardware-live/hud-serial.log`

This coexists with the earlier boot capture's A2DP/I2S initialization **FAIL**:
`I2S: register I2S object to platform failed` (see
[earlier serial log](../evidence/20261028/hardware-audit-run/hud-serial.log)).
A later generic audio-task startup line is not proof that I2S registered. The
user's historical report of audible output is retained as user-reported only;
no live music was played or independently confirmed during this turn.

The capture spans uptime 154–180 s (26 s), with six telemetry samples and no
reset or fault line observed. Heap readings were free 39,960–40,088 B, minimum
32,716 B, and largest block 18,420 B. Render telemetry was 546,354–549,552
pixels/s (`fps_equiv=3`), average frame 35–36 ms, and maximum frame 321 ms. The
sample is useful runtime evidence, not a visual or interaction test; the maximum
frame metric exceeds the configured 100 ms hard limit and needs a separate
interactive investigation. No live Maps or music playback was initiated because
the physical handset was not connected to ADB.

**Current-turn hardware verdict:** Maps over BLE and music over Bluetooth A2DP
are **NOT RUN**. The previous session's user-reported successful operation and
partial evidence above remain historical, not a substitute for this requested
repeatable test. Music transport is Classic Bluetooth A2DP to the HUD speaker;
navigation/status synchronization uses BLE GATT. They are distinct links.

The earlier safe-stop note applies to the session in which it was recorded. This
follow-up only read the active serial stream without resetting or otherwise
changing the hardware.

## Still blocked or not fully evidenced

- Raw touch calibration at all four corners and center; rotation/mirror transform and physical hitbox edge tests.
- Exact on-glass CYD screenshot or recorded pixels for Maps, media, and modal screens.
- Firmware image hash/build ID read back from the connected board, so correspondence to the newly compiled image is unverified.
- Controlled music-track identification, measured A2DP duration/reconnects, protocol round trip, settings persistence, failure recovery, and soak testing.
- Android-side automated permission/process-death/orientation and accessibility target matrix.

These are not claimed as passes. Existing separately captured Android build, firmware build, host tests and static checks are listed above; rebuild/retest the final working tree before release where necessary.
