# Test Procedure

## Stage 0 — Host regression (no hardware)

```bash
./run-tests.sh        # or run-tests.cmd
```

All four suites must print PASS and the runner must end with
`HOST TESTS: ALL PASSED` (exit code 0).

## Stage 1 — Flash & display (env:cyd)

1. `cd firmware && pio run -e cyd -t upload && pio device monitor`
2. Expect the boot banner `=== CYD Head-Unit ===` at 115200 and
   `[init] display... ok`.
3. HOME screen must render with the backlight on. If the image has tearing or
   garbage, set `TFT_SPI_CLOCK` to 20 MHz in `config_pins_cyd.h` and retest.
4. Type `STATUS` — expect `build: HARDWARE`, `gps: NO DATA / no fix` (indoors).

## Stage 2 — GPS

1. Connect the NEO-6M to the JST port, place near a window.
2. `GPSRAW` — raw NMEA sentences must echo (`$GPGGA...`, `$GPRMC...`).
3. Within ~30 s the status chip should move `NO GPS DATA` → `WAIT FIX` →
   `GPS FIX`. `STATUS` must show `receiving` and a growing `chars` count.
4. Unplug the GPS: chip must return to `NO GPS DATA` within 5 s
   (`GPS_STALE_MS`), while `receiving` drops after 3 s (`GPS_RECEIVING_MS`).

## Stage 3 — Touch (TOUCHTEST)

1. Type `TOUCHTEST` — taps now print `[TOUCH] x=… y=…` and the UI is paused.
2. Tap the four screen corners and the centre. Record readings.
3. If coordinates are mirrored or offset, adjust `TS_MIN`/`TS_MAX` and
   `TOUCH_OFFSET_X/Y` in `src/hal/touch_hal.cpp` until (0,0) and (319,239)
   land correctly. Presses held >1 s are rejected by design (ghost/stuck
   protection).
4. `TOUCHTEST` again to return to normal operation.

## Stage 4 — UI walkthrough (regression vs docs/FEATURE-MATRIX.md)

1. HOME → each of DASHBOARD / NAVIGATION / MEDIA / SYSTEM → back chevron.
2. Status-bar DAY/NIGHT label toggles the theme from every screen; the
   SYSTEM button does the same.
3. DASHBOARD: speed, heading, trip, max, **avg**, time update; SWITCH NAV
   opens NAVIGATION; RESET TRIP zeroes the trip fields.
4. NAVIGATION: turn icon + distance + street banner update ~2 Hz while
   moving; banner shows `Waypoint n/4`.
5. MEDIA: transport buttons change state/progress; VOL± moves the slider
   (on hardware without `AUDIO_ENABLED` the action is silent by design).
6. SYSTEM: nine diagnostic rows; heap refreshes every ~5 s; uptime counts.

## Stage 5 — Console regression

Exercise every command from README § Serial console, including:

- `CUE LEFT|250 m|MG Road|12 min|40` → accepted, drives the NAV screen,
  and **expires after 10 s** back to GPS/no-route state.
- Malformed `CUE` lines must be rejected with the usage hint.
- A 160+ character line must be discarded whole (`line too long`).
- `TRIPRESET`, `NAVRESET`, `THEME`, `GPSRAW`, `HELP`.

## Stage 6 — Soak (stability)

Run the unit for ≥30 minutes with GPS live. Watch `STATUS` free-heap:
it must stabilise (± a few hundred bytes), not decline monotonically.
All state in this firmware is statically allocated; a steady downward heap
trend indicates a regression and must be investigated before release.
