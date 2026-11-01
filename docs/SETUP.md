# Setup

Plug-and-play setup for the Android companion app and DashCue HUD. Follow the
board-match check before flashing: the firmware pin map and display driver are
hardware-specific, and this checkout contains historical reports for more than
one CYD revision. Do not assume that a board with the same "Cheap Yellow Display"
name has the same panel or touch wiring.

---

## 0. Prerequisites

* A PC with Python 3.10+ and network access for the first build.
* The **ESP32-3248S035, 3.5-inch 480×320 ST7796** board profile documented by the
  current firmware (`cyd-3248s035r-st7796`), and a USB data cable. Confirm the
  serial banner or board marking before flashing. If your hardware is instead
  ESP32-2432S028R, stop: its 320×240 display and separate touch-SPI wiring do not
  match the current configured product pin map.
* A phone on Android 8.0 (API 26) or newer, plus Android platform tools (`adb`)
  if installing from a computer.
* Python 3.10+, Java 17+, PlatformIO Core, and network access for first-time build
  and pinned dependency downloads.
* Optionally, an OSRM endpoint URL for navigation (see [ROUTING_SETUP.md](ROUTING_SETUP.md)).
  Without one the app runs, but navigation cannot be started, there is no built-in
  default and no hidden server (DEC-14).

Verify the host with:

```bash
python tools/setup_check.py
```

It prints one line per prerequisite and exits non-zero if a required one is missing,
including the exact command that fixes it. Its output is the GATE-01 evidence.

## 1. Build and flash the matching HUD profile

First inspect the board's model/serial banner and verify the intended screen is
**480×320 ST7796**. The current configured product environment is named
`cyd_ili9341` for historical reasons but actually selects `ST7796_DRIVER`; use it
only for the matching board profile. The source currently configures the touch
controller on shared display SPI pins. Earlier reports for the 2.8-inch 2432S028R
record a different touch bus, so flashing this build to that revision can leave
touch/display behavior incorrect. See [hardware report](HARDWARE_REPORT.md) and
[hardware validation](testing/hardware-validation.md); those records are not a
substitute for inspecting your board.

```bash
python tools/setup_check.py
cd firmware
pio run -e cyd_ili9341
# Substitute the serial port reported by `pio device list`.
pio run -e cyd_ili9341 -t upload --upload-port COM4
pio device monitor -p COM4 -b 115200
```

A successful build proves compilation only. After flashing, check the serial
banner for `hw=cyd-3248s035r-st7796`, `BOOT boot_to_ui_ms=...`, BLE advertising,
and the touch/calibration status. Stop and investigate if the board identity is
unexpected or if boot reports a display, touch, BLE, or audio error. Do not erase
NVS as a routine install step: it can remove calibration and saved settings.
The firmware has no OTA update path. On Linux, serial access may require the
appropriate device permissions; on Windows, install the CH340/CP210x driver if
needed.

## 2. Confirm the hardware

```bash
cd tools/hw_probe
pio run -t upload --upload-port COM4
python tools/test-only/serial_harness/capture.py --port COM4 --seconds 16 \
    --out docs/evidence/$(date +%Y%m%d)/TC-HW-01/hw_probe.log
```

The probe prints a measured fact sheet and draws a colour-bar test pattern, then a
crosshair that should follow your finger. Two things are worth checking by eye:

* **the crosshair tracks your finger at the same place**, if it is mirrored or
  offset, calibration is wrong;
* **orientation**, a 180°-rotated image means the mount orientation needs the
  rotated setting.

## 3. Install the phone app

For a local developer build, no signing setup is needed:

```bash
cd android
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.dashcue.app/.MainActivity
```

For a locally signed release APK, generate the private development key once,
then build and install it:

```bash
cd ..
python tools/make_dev_keystore.py
cd android
./gradlew :app:assembleRelease
adb install -r app/build/outputs/apk/release/app-release.apk
```

Keep the generated signing key and `android/keystore.properties` backed up and
private; they are local and must never be committed. Future in-place release
upgrades must use the same key. If you do not have the original key, Android
requires uninstalling the old app first, which deletes its local app data.

## 4. Grant Android permissions

The app explains its permissions during onboarding. In particular:

* **Nearby devices** (Android 12+) or **Location** (older versions) for BLE;
* **Location** is required for turn-by-turn navigation; Android 11 and earlier
  also require it for BLE scanning. Location is optional for basic BLE connection.
* **Notifications** allow the connection/navigation foreground notification.
* **Notification access** enables media and navigation notification reading. On
  Android 13+ this can be blocked for
  sideloaded apps: the toggle appears greyed out under "Allow restricted settings".
  The app detects that and explains the exact steps; it does not pretend to be enabled.

## 5. Connect the phone to the HUD

The current BLE GATT data link has **no pairing or passkey step**. On first run,
complete the Android permission screen, ensure Bluetooth is enabled, and tap
**Connect** on the Dashboard. The app scans for `DashCue-HUD`, connects, performs
GATT service discovery and protocol synchronization, and then reports **Connected**.
Use **Refresh** to retry a stalled scan. **Disconnect** stops the current app
connection; it does not erase Android Bluetooth settings.

This is intentionally an unauthenticated BLE link: nearby BLE clients can attempt
to connect and send protocol frames. CRC protects against accidental corruption,
not malicious access. Do not use the device where that exposure is unacceptable;
see [threat model](security/threat-model.md).

Classic Bluetooth A2DP audio is a separate link and may show the Android system's
normal Bluetooth confirmation when connecting the phone's media audio to the HUD.
Do not enter a fixed PIN. If Android keeps showing an old pairing-code dialog for
DashCue, remove a stale `DashCue-HUD` entry from Android Bluetooth settings, then
retry the BLE connection.

## 6. Configure routing

Open **Settings → Routing endpoint** and enter your OSRM base URL, then use the
connection test. Options, including self-hosting, are in
[ROUTING_SETUP.md](ROUTING_SETUP.md). The public OSRM demo server is not a supported
endpoint and is deliberately not offered as a default.

## 7. Mount and check

1. Mount the HUD so it does not obstruct the driver's view.
2. Power it from a USB car charger; a weak supply causes brownouts and visible resets.
3. Start the car and confirm the phone reconnects without any taps (scenario **S2**,
   target ≤ 15 s).
4. Check the driving lock: while stationary the settings screens open; above 8 km/h
   they do not (DEC-28).

## Troubleshooting

| Symptom | Likely cause | Action |
|---|---|---|
| Nothing on the display or distorted colors | firmware profile does not match the panel controller | stop; identify the exact board revision and select/build a matching profile; do not guess between drivers |
| Display works, touch dead or inaccurate | touch bus/pin map or calibration does not match the board | check the board profile and serial touch/calibration messages; do not flash the 3.5-inch profile onto a 2.8-inch board |
| Crosshair mirrored or offset | calibration missing or invalid | re-run touch calibration |
| Phone never reconnects | battery optimisation killed the app, or Bluetooth is off | check the app's health checks screen; follow its battery guidance |
| Media controls show "not supported" | the app's session does not expose that action | expected; the HUD only enables actions the session advertises |
| Routing fails | no endpoint configured, or the endpoint is unreachable | configure and test in Settings |
