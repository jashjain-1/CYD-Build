# Setup

First-time setup for the HUD and the phone app. Scenario **S1** requires a
non-expert to complete this from the README alone; the manual path below exists so
that the README can be kept short.

> **Status:** the Android app does not exist yet, so steps 3 onwards are the planned
> procedure, not a tested one. Steps 1–2 have been run on real hardware. Anything not
> yet verified is marked `NOT RUN` rather than described as working.

---

## 0. Prerequisites

* A PC with Python 3.10+ and network access for the first build.
* An ESP32-2432S028R board and a USB data cable (not a charge-only cable).
* A phone on Android 8.0 (API 26) or newer.
* Optionally, an OSRM endpoint URL for navigation (see [ROUTING_SETUP.md](ROUTING_SETUP.md)).
  Without one the app runs, but navigation cannot be started — there is no built-in
  default and no hidden server (DEC-14).

Verify the host with:

```bash
python tools/setup_check.py
```

It prints one line per prerequisite and exits non-zero if a required one is missing,
including the exact command that fixes it. Its output is the GATE-01 evidence.

## 1. Flash the HUD

```bash
cd firmware
pio run -e cyd_ili9341 -t upload --upload-port COM4
pio device monitor -b 115200
```

* Use `-e cyd_st7789` if the panel needs the ST7789 driver (the display stays blank or
  scrambled with the wrong one).
* The flash command is the only firmware update path in v1: there is no OTA (§2.3).
* On Linux, if the port cannot be opened, add your user to the `dialout` group; on
  Windows, install the CH340 driver.

**Measured on the board used for development:** 4 MB flash, no PSRAM, touch on its own
SPI bus. See [HARDWARE_REPORT.md](HARDWARE_REPORT.md).

## 2. Confirm the hardware

```bash
cd tools/hw_probe
pio run -t upload --upload-port COM4
python tools/test-only/serial_harness/capture.py --port COM4 --seconds 16 \
    --out docs/evidence/$(date +%Y%m%d)/TC-HW-01/hw_probe.log
```

The probe prints a measured fact sheet and draws a colour-bar test pattern, then a
crosshair that should follow your finger. Two things are worth checking by eye:

* **the crosshair tracks your finger at the same place** — if it is mirrored or
  offset, calibration is wrong;
* **orientation** — a 180°-rotated image means the mount orientation needs the
  rotated setting.

## 3. Install the phone app

```bash
python tools/make_dev_keystore.py        # once per machine; creates a local signing key
cd android
./gradlew :app:assembleRelease
adb install -r app/build/outputs/apk/release/app-release.apk
```

The keystore is generated locally and never committed (DEC-25). **Every future upgrade
must use the same key**, or `adb install -r` fails with
`INSTALL_FAILED_UPDATE_INCOMPATIBLE` and the app has to be uninstalled first, which
discards its settings and any stored destinations.

## 4. Grant permissions

The onboarding wizard asks for each permission once, in plain language, and shows
whether it is actually granted. [PERMISSIONS.md](PERMISSIONS.md) explains what each one
is for and what the app does without it. In particular:

* **Nearby devices** (Android 12+) or **Location** (older versions) for BLE;
* **Precise location while using the app** for navigation — background location is
  never requested (DEC-19);
* **Notifications** for the navigation foreground service;
* **Notification access** for media control. On Android 13+ this can be blocked for
  sideloaded apps: the toggle appears greyed out under "Allow restricted settings".
  The app detects that and explains the exact steps; it does not pretend to be enabled.

## 5. Pair the phone with the HUD

Pairing is always an explicit action on both devices; there is no silent pairing.

1. On the HUD: **More → Pair phone**, press and hold for 3 seconds.
2. The HUD shows a 6-digit passkey and a 120-second countdown.
3. On the phone: **Pair HUD**; Android's association dialog appears (CompanionDeviceManager).
4. Confirm the passkey matches on both devices.

If the passkey differs, **cancel**: that is the MITM protection doing its job. On
timeout the window closes by itself; nothing is left listening.

To remove a bond, use **More → Forget phone** on the HUD *and* unpair on the phone.
Either side alone leaves a half-bond, which is why both are documented.

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
| Nothing on the display, backlight on | wrong driver configuration for the panel revision | try `-e cyd_st7789` |
| Display works, touch dead | the board routes touch on the other SPI bus | run the probe scan; it reports which bus answered |
| Crosshair mirrored or offset | calibration missing or invalid | re-run touch calibration |
| Phone never reconnects | battery optimisation killed the app, or Bluetooth is off | check the app's health checks screen; follow its battery guidance |
| Media controls show "not supported" | the app's session does not expose that action | expected; the HUD only enables actions the session advertises |
| Routing fails | no endpoint configured, or the endpoint is unreachable | configure and test in Settings |
