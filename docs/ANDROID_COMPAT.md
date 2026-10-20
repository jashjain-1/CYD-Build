# Android compatibility

§5.4: the declared support range is "Android 8.0 (API 26) and newer **as tested**", and
"an APK installing is not the same as supported".

**Status: no Android testing has been performed.** The app does not exist yet (M4).
This file exists so the matrix has one home and cannot be assembled from memory later,
and so that "not tested" cannot be mistaken for "tested and fine".

---

## 1. Tested devices

| Device | Model / codename | Android | OEM skin | Result | Evidence |
|---|---|---|---|---|---|
| OnePlus (attached over adb, `CPH2585IN`) | CPH2585 | not yet recorded | OxygenOS | **NOT TESTED** |, |

Device details (exact Android version, build fingerprint, OEM battery-management
behaviour) will be recorded here when GATE-07 and GATE-09 run in M4, captured from the
device itself rather than from a specification sheet:

```bash
adb shell getprop ro.build.version.release
adb shell getprop ro.build.version.sdk
adb shell getprop ro.build.fingerprint
adb shell getprop ro.product.manufacturer
```

## 2. What must be recorded per device

| Item | Why it is a row in this table and not a footnote |
|---|---|
| BLE permission model (API 31+ vs below) | the two models are different code paths, and only one is exercised per device |
| CompanionDeviceManager association behaviour, and whether presence observation wakes the app | GATE-08/09; the association is what makes reconnection work without scanning |
| Notification-listener grant, including the Android 13+ restricted-settings block for sideloaded builds | determines whether media control is possible at all on that device (TC-UI-03) |
| OEM battery management: does it kill the session, and what survives | recovery is best-effort and must be documented per device, not promised in general |
| `getActiveSessions` result for each media app | feeds the media compatibility matrix |
| Foreground-service type enforcement | a start failure on API 34+ is a hard error, not a warning |

## 3. Emulator and non-physical testing

BLE hardware tests need a physical phone, so emulators can only cover the
hardware-independent parts: permission request flows, state-machine transitions (TC-AN-01/02),
settings persistence, and UI states. Emulator results are recorded with the emulator's
API level and are explicitly marked as emulator evidence, never mixed into the
physical-device rows above with a PASS.

## 4. Claim policy

Until this table has at least two physical devices on different API levels, the
compatibility statement is:

> Tested on one device only; other devices and OS versions are `UNTESTED`.

That is narrower than the spec's aim (§5.4 asks for "one physical phone plus emulator
or test devices for other API levels") and is recorded as such rather than quietly
widened.
