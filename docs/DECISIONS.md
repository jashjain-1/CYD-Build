# Architecture Decision Records — CYD-Build

Numbering restarts at 0001: this is an independent project.

## ADR-0001 — Clean-room reimplementation, same hardware

**Context:** Rebuild the head-unit firmware for the identical ESP32-2432S028R
board without sharing code, history or build state with any prior repository.
**Decision:** New sources, new git history, pinned dependencies, same pin map
and same verified behaviour. **Consequences:** no legacy baggage; behaviour is
locked by host tests rather than by code lineage.

## ADR-0002 — PlatformIO environments mirror all supported wirings

**Decision:** `cyd` (default), `esp32dev`, `esp32dev_audio`, `sim`; a single
`config_pins.h` dispatcher selects the pin header and `#error`s when no target
is defined. **Consequences:** an env forgotten in CI fails loudly instead of
building with the wrong pins.

## ADR-0003 — Audio stays disabled by default

**Decision:** `AUDIO_ENABLED=0` in every env except `esp32dev_audio`. The
onboard amp (GPIO 26) has no verified attenuation chain; PWM duty is not a
power limiter. **Consequences:** media controls are silent until the audio env
is explicitly chosen.

## ADR-0004 — Safety policy stays pure and host-tested

**Decision:** GPS reception/staleness verdicts live in
`hal/gps_status_policy.h` as a pure function over a snapshot
(`GPS_STALE_MS = 5000`, `GPS_RECEIVING_MS = 3000`, both boundaries exclusive).
**Consequences:** the exact logic that decides "is this fix trustworthy" is
unit-tested on every commit without hardware.

## ADR-0005 — Dependency pins resolved against the live registry

**Context:** The registry no longer serves `Adafruit ILI9341 @ 1.6.0`, and
`XPT2046_Touchscreen` is only published as an alpha version.
**Decision:** Pin ILI9341 to **1.6.4** (same 1.6.x API line) and pin
XPT2046_Touchscreen to the canonical GitHub tag **v1.4**. Both choices are
documented in `firmware/platformio.ini`.
**Consequences:** builds are reproducible; the legacy DevKit env compiles.

## ADR-0006 — Touch filtering: median-of-3 + press→release taps

**Decision:** Three independent ADC samples per axis reduced by a median filter;
a tap fires only on release and presses >1 s are rejected as stuck/ghost.
The CYD touch MISO sits on input-only GPIO 39, so the driver bit-bangs SPI.
**Consequences:** spike-free coordinates and no phantom taps while dragging.

## ADR-0007 — Per-region dirty rendering

**Context (optimisation pass):** the baseline repainted every dynamic box on
each 2 Hz/1 Hz tick whenever *any* nav field changed, and the SYSTEM screen
erased and redrew all nine diagnostic rows every second regardless of change.
**Decision:** nav-derived state is diffed into per-region dirty bits
(6 dashboard fields, 3 navigation cards) and SYSTEM rows are cached as
(text, colour) pairs and repainted only when they differ.
**Consequences:** a steady-state 1 Hz drive-time tick repaints 1 of 5
dashboard regions; the SYSTEM screen reaches zero SPI writes in steady state.
Function output is unchanged — verified by the 4-suite host matrix and the
`pio run` gates.

## ADR-0008 — Touch targets padded beyond their visual boxes

**Context:** research (Fitts's law; embedded TFT ergonomics guides) puts the
minimum target near 44 px; the baseline back chevron, volume keys and dash
action bar were 36–42 px in their smallest dimension.
**Decision:** enlarge **hit rectangles only** (back 48×48, volume 48×44,
dashboard actions 150×44, theme zone 70×32) without moving a single pixel of
drawn UI. **Consequences:** easier operation, zero visual regression.

## ADR-0009 — GPS status chip as a HOME-screen shortcut

**Decision:** tapping the GPS chip on HOME opens NAVIGATION; on other screens
it stays inert. **Consequences:** one-tap access to guidance without risking
accidental navigation away from the current screen.

## ADR-0010 — No dynamic allocation anywhere

**Context:** long-running embedded UIs degrade when they fragment the heap.
**Decision:** all buffers, strings and state are statically sized
(`ConsoleLine[160]`, fixed `char[N]` fields, static caches).
**Consequences:** heap is flat by construction; Stage 6 of the test procedure
monitors it anyway as a tripwire.
