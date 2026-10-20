# Known issues

Current status (2026-10-17): see [RELEASE_READINESS.md](RELEASE_READINESS.md) and
[HARDWARE_RECOVERY.md](HARDWARE_RECOVERY.md). These supersede the early milestone
inventory retained below.

- **Fixed in source:** the HUD has three tabs (Media/Nav/More) and no schematic map;
  the Android app has four wired tabs; all host and Android unit suites pass.
- **Closed, memory gate:** static DRAM is **69,780 B against the 70,000 B gate**
  (`python tools/memory_report.py firmware/.pio/build/cyd_ili9341/firmware.elf`).
  The 8,192 B artwork buffer moved from `.dram0.bss` to a lazily allocated heap
  block (`heap_caps_malloc` on device), and 14 unused LVGL widgets were switched
  off plus the duplicated touch-filter/calibration translation units deleted. No
  feature was removed: the buffer is the same size, allocated once on the first
  transfer and never freed, so there is no 8 KB alloc/free cycle to fragment the
  heap. Headroom is only 220 B, so the next static allocation needs a plan.
- **Blocked, physical:** no CYD serial port in this session, so the rebuilt firmware
  has not been flashed and runtime heap/touch/BLE/LVGL timings are not re-measured.
- **Blocked, visual:** the phone is on a secure lock screen (`deviceLocked=1`); the
  APK installs and the process starts, but no four-tab screenshot exists.
- **Blocked, routing:** no OSRM/Nominatim endpoint was supplied (GATE-10), so search,
  route preview and turn guidance have not run end to end.
- **Unverified, artwork:** the ARTWORK_BEGIN/CHUNK/END consumer *is* wired
  (`proto_task.cpp` → `artwork_store` → media screen), but end-to-end delivery from a
  real player on a real phone has not been observed and the panel has not been
  photographed, so "the HUD shows album art" is not yet a verified claim.
- **Unverified:** secure pairing re-test, physical calibration/persistence, reconnect,
  API compatibility, release security, eight-hour soak and vehicle UX.

## Historical M1 inventory

The statements below describe an earlier milestone, not the present code. In
particular Android, generated protocol code, calibration and a Bluedroid peripheral
now exist. Historical omissions are not current implementation assessments.

---

## 1. Open defects

| ID | Severity | Issue | Workaround / next step |
|---|---|---|---|
| KI-01 | medium | The display controller cannot be identified: RDDID returns `0x0000`. A panel needing a different driver configuration would present as "display dead" with no diagnostic. | The `cyd_st7789` environment exists; the probe draws a test pattern so a wrong driver is visible to an operator. |
| KI-02 | medium | Touch calibration does not exist yet, so no on-screen target is trustworthy and TC-HW-04 (uncalibrated boot safety) cannot pass. | Probe-only for now; calibration is M1 work. |
| KI-03 | low | Touch has no IRQ line, so a press is only noticed by polling. The measured poll rate (4 160 Hz) leaves ample margin, but battery/wake behaviour is measured later. | None needed yet; re-measure under TC-PERF-01. |
| KI-04 | low | A saturated touch read (`0xFFFF`) is indistinguishable from a press if a consumer forgets the plausibility check. Two places currently implement the check independently. | The check exists in both the probe and the app; M2 should move it behind the single `ITouchSource` DEC-04 requires so it cannot be forgotten. |

## 2. Unverified behaviour that is claimed to work nowhere

These compile and are partly measured, but no document claims they work:

| Area | Status |
|---|---|
| LVGL UI | LVGL 9.6.0 compiles with TFT_eSPI and a generated config; **no LVGL runtime measurement exists** (GATE-03 is `PARTIAL`). |
| BLE peripheral | NimBLE-Arduino 2.5.1 is pinned and compiles; it has never been initialised on the board. |
| Protocol codec | `protocol/dashcue.proto` is written and `.options` caps every string/array; the **nanopb codec is generated** and its caps verified by inspection. **No fixture has been round-tripped and no envelope/framing code exists** (M2). |
| Android app | Does not exist yet. Kotlin/Gradle/AGP versions are pinned from registry metadata; **no Gradle build has been run**. |
| Media control, navigation, settings, diagnostics | Do not exist. |
| Wi-Fi map channel | Not started (optional, M8). |

## 3. Environmental limitations on this machine

| Limitation | Effect |
|---|---|
| Only one board (`cyd_2432s028r`, touch on its own SPI bus) | Cannot verify the shared-bus revision. The probe handles both by scanning. |
| Only one phone (OnePlus CPH2585) | `ANDROID_COMPAT.md` will describe one device, not a matrix. §5.4 requires more than one for the compatibility claim, so the claim will be narrower than the spec's ambition and will be stated as such. |
| No OSRM or Nominatim endpoint configured | GATE-10 is `BLOCKED`; navigation tests will be `BLOCKED` until the operator supplies one (DEC-14 forbids a baked-in default). |
| No second host for a clean-host setup test | TC-E2E-01 stays `NOT RUN` (see SI-03). |
| No Gradle CLI installed | The Gradle wrapper is used; the wrapper JAR must be fetched or generated on first use. |

## 4. Known-broken inputs

| Input | Behaviour | Note |
|---|---|---|
| Street names outside Latin-1/Latin Extended-A | Not yet handled; the transliteration pipeline (DEC-21) is M6 work. | The HUD font subset and the phone-side transliteration must land together, or text renders as missing glyphs. |
| Text longer than a `.options` cap | Rejected by the codec rather than truncated silently, by design, but the sender must therefore truncate on code-point boundaries first (DEC-21, TC-PR-13). | Not yet implemented. |
| Touch on a floating bus | Rejected as no-signal (KI-04). | Verified in the probe; not yet in the app's HAL. |
| A `.options` entry naming a field type that nanopb does not define | Generation fails loudly rather than ignoring the option. | Verified: three `type:FT_FLOAT` lines in the options file were rejected by the generator and removed. |
| Regenerating protocol code when nanopb has not been installed | `tools/gen_proto.py` exits with the exact `pio run` command that installs it. | Verified by construction; the failure path names the command. |
