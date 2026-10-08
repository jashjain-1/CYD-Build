# CYD-Build — Feature Matrix (Regression Baseline)

This inventory is the **regression baseline** for the post-build optimization
pass. Every row must still work after the pass. Verification column shows how
each feature was (or will be) checked.

## Screens (5)

| # | Screen | Contents / features | Verify |
|---|--------|---------------------|--------|
| 1 | HOME | Title bar, 4 nav buttons (DASHBOARD / NAVIGATION / MEDIA / SYSTEM) with icons, GPS status chip | compile + visual |
| 2 | DASHBOARD | Speed readout, heading + cardinal, trip distance, max speed, drive time, SWITCH NAV button, RESET TRIP button, back button | compile + visual |
| 3 | NAVIGATION | Turn icon card, distance, maneuver text, speed card, heading card, ETA/bearing card, street banner, source line (GPS waypoint vs external cue), back button | compile + visual |
| 4 | MEDIA | Track card (title/artist), progress bar, PREV/PLAY-PAUSE/NEXT transport, volume slider + VOL−/VOL+, status line, back button | compile + visual |
| 5 | SYSTEM | 9 diagnostic rows (Theme, Build, GPS, NMEA chars, Display, Audio, Heap, Uptime, Firmware), TOGGLE DAY/NIGHT button, back button | compile + visual |

## Persistent UI

| Feature | Detail | Verify |
|---------|--------|--------|
| Status bar on every screen | title, HW/SIM badge, DAY/NIGHT indicator | compile |
| GPS status chip | EXTERNAL CUE / GPS FIX / WAIT FIX / NO GPS DATA, 2 Hz refresh with change-only redraw | compile |
| Back button | top-left chevron on all non-home screens | compile |
| Day/Night theme | tap status-bar theme zone, THEME console cmd, SYSTEM button; full palette swap | host-visual / compile |
| Dirty-region rendering | dynamic areas repaint only when state changes (nav 2 Hz, sys 1 Hz) | code inspection |

## Serial console (115200 baud)

| Command | Behavior | Verify |
|---------|----------|--------|
| STATUS | full diagnostics dump | compile |
| TOUCHTEST | toggle tap echo, UI paused | compile |
| GPSRAW | raw NMEA echo on/off | compile |
| HOME / DASH / NAV / MEDIA / SYS | screen switching | compile |
| CUE <icon\|dist\|street\|eta\|speed> | external phone cue, 10 s expiry | **host test** |
| THEME | day/night toggle | compile |
| TRIPRESET | trip odometer reset | **host test** |
| BT | SIM only: toggle simulated connection; HW: explanatory message | **host test** (SIM) |
| NAVRESET | full nav reset | **host test** |
| TAP x y | SIM only: inject tap; HW: explanatory message | compile |
| unknown command | help listing of all commands | compile |
| overflow line (>159 chars) | discarded whole, never executed truncated | **host test** |

## Services / background processes

| Process | Cadence | Verify |
|---------|---------|--------|
| gpsUpdate (drain NMEA → TinyGPSPlus, ≤256 B/loop) | every loop | compile |
| navUpdate (guidance + trip odometer) | 1 Hz | **host test** |
| mediaTick (progress + blip sequencing) | every loop | **host test** |
| systemTick (heap cache refresh) | 5 s | compile |
| handleConsole (≤64 B/loop) | every loop | **host test** |
| touchPollTap (bit-bang, ≥20 ms apart) | every loop | compile |
| uiTick (2 Hz dynamic / 1 Hz sys / GPS chip) | every loop | compile |
| SIM GPS NMEA generator (1 Hz checksummed sentences) | SIM only | **host test** (NMEA framing) |

## Hardware functions

| Function | Interface | Verify |
|----------|-----------|--------|
| ILI9341 HSPI 320×240, 40 MHz, rotation 1 | display_hal | `pio run -e cyd` |
| Backlight GPIO21 driven HIGH | display_hal | compile |
| XPT2046 bit-bang SPI (CLK25/CS33/MOSI32/MISO39), median-3, press→release tap, ≥20 ms poll | touch_hal | compile |
| NEO-6M UART2 RX27/TX22 @9600 + TinyGPSPlus | gps_hal, gps_service | compile |
| GPS staleness policy (STALE 5 s / RECEIVING 3 s, exclusive boundary) | gps_status_policy | **host test** |
| Optional LEDC tone output (AUDIO_ENABLED=0 default) | audio_hal | compile |
| Legacy DevKit target (VSPI + XPT2046 lib) | env:esp32dev | compile (optional env) |

## Navigation logic

| Feature | Detail | Verify |
|---------|--------|--------|
| 4-waypoint demo route, 25 m arrival radius, loops | demo_route, nav_service | **host test** |
| Turn icon classification (±15°/±45°/150° thresholds) | nav_service | **host test** |
| Haversine distance / bearing / turn math | nav_math | **host test** |
| Trip odometer: teleport/stall rejection, rebase recovery, max/avg speed, drive time | nav_service | **host test** |
| Phone cue: 10 s expiry, validation (speed 0–300, field length, ASCII) | phone_cue, nav_service | **host test** |
| millis() wraparound safety | nav_service | **host test** |

## Media logic

| Feature | Detail | Verify |
|---------|--------|--------|
| Play/pause/prev/next, 3 demo tracks (SIM), 180 s auto-advance | media_service | **host test** (SIM) |
| Volume clamp 0..10 | media_service | **host test** |
| Two-tone blip sequencer (90 ms + duration, then stop) | media_service | **host test** |
| Connected/disconnected gating (SIM) | media_service | **host test** (SIM) |

## Persistence / modes / startup

- No persistent storage (no NVS/FS usage) — nothing to regress.
- Build modes: `cyd` (default), `esp32dev`, `esp32dev_audio`, `sim` via `SIM_BUILD`.
- Startup: serial 115200 → 50 ms settle → display → touch → GPS → audio →
  services → UI (HOME) → command banner. Loop never blocks beyond `delay(1)`.
