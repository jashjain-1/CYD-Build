# Release readiness — 2026-10-16

**NOT READY.** The hardware repair exists in source. There is no fresh ESP32
target build, flash, Android build/install, or real phone↔HUD E2E result from this
session. The user reports a blank display after a calibration stall. That symptom
has not been physically resolved or observed by the agents.

## Changes prepared

- Correct custom touch SPI initialization order, remove unverified calibration
  fallback, keep controls locked on invalid calibration/save failure, and report
  touch detection and calibration samples/results.
- Allocate only the current UI screen, restore it from locked state snapshots,
  and replace large queued state copies with compact events. Fresh host layout:
  80-byte UI event / 1,280-byte queue; command 768 bytes and BLE RX 8,256 bytes.
- Check failed startup allocations/task creation, and remove the no-PSRAM
  board's misleading `BOARD_HAS_PSRAM=0` define. No memory pool or stack size was
  guessed to obtain a nominal pass.
- Accept a valid maximum-size frame with a short final fragment; enforce bounds.
- Correct Android pairing-result delivery, association lookup, GATT descriptor
  sequencing, acknowledged fragment writes, MTU/payload caps, and retry teardown.
- Replace simulation-based release claims with explicit evidence scopes. Add
  `tools/memory_report.py` and `tools/run_hardware_checks.py` for repeatable audits.

## Evidence from this session

| Check | Result and limit |
|---|---|
| Firmware native tests | **PASS 22/22**, actual protocol/state/touch sources with pinned Nanopb and Unity. `evidence/20261016/current-firmware/native-tests.log`. Not FreeRTOS/device execution. |
| UI syntax | **PASS**, 13 UI translation units against pinned LVGL headers. `evidence/20261016/current-firmware/ui-syntax.log`. No display rendering or full target link. |
| Test reporting | **PASS 5** regressions. `evidence/20261016/current-tests/runner-regressions.log`. |
| Adversarial host tests | **PASS 10**, includes fresh native 10k-input run; see `evidence/20261016/current-tests/result.json` for source/campaign scope. |
| Earlier native 1M-input campaign | **PASS** before the subsequent firmware frame fix. Hash-bound historical result within this session; not the final source verification. |
| Latest native 1M-input campaign | Compiled, **BLOCKED** at execution by Windows Application Control, WinError 4551. No bypass attempted. |
| Hardware-report classification | **PASS 8** host regression cases. `evidence/20261016/current-hardware/report-classification-tests.log`. No hardware input. |
| Environment-report recovery | **PASS 1** regression: inaccessible SDK reports BLOCKED without aborting remaining checks. `evidence/20261016/current-hardware/setup-report-tests.log`. |
| Current tool environment | **FAIL/BLOCKED**, recorded in `evidence/20261016/current-hardware/current-sandbox-setup.log`. |
| Legacy 265-case model suite | **BLOCKED** this run: accessible Python lacks pytest. Prior 265 PASS never represented physical E2E. |
| Android tests/build | **BLOCKED** by Gradle cache/toolchain access. Added tests are not counted as passed. `evidence/20261016/current-android/baseline-gradle.log`. |
| Flash, boot, five-point touch, BLE pairing, runtime memory | **BLOCKED** by toolchain/USB access review failure. No current hardware PASS. |

## Memory and artifact identity

The pre-existing ELF has 68,044 bytes static DRAM. Its 1,732,160-byte binary has 44.94%
headroom in the 3 MiB app partition. Its SHA-256 is
`7bcc18e77ff5d4c42d19c14ac5303a7bbef17f4b786647ff66a914f4b0962153`.
It **predates this repair**. Full section/hash output is in
`evidence/20261016/current-hardware/existing-artifact-memory.json`.

Historical runtime heap fails the target: free 27,884 / minimum 26,364 /
largest 9,204 bytes. A new build must demonstrate minimum ≥ 40,960 and
largest ≥ 20,480 during real BLE/UI traffic, followed by an eight-hour soak.
Source-level allocation savings cannot establish those results.

No new release APK or firmware binary is delivered. Source version fields remain
1.0.0; they cannot identify these uncommitted changes. Existing signing credentials
were not read or changed. No Git commit, reset, erase, or NVS removal was performed.

## Remaining blockers and product gaps

- Automatic approval review failed before installed Python/PlatformIO, Android
  toolchain/cache, and USB device commands could execute, despite explicit user
  approval. The service returned an error, not an unsafe-action determination.
- Physical panel identity/colors, calibration accuracy/persistence, secure pairing,
  phone lifecycle/reconnect, queue pressure, stack headroom, rendering performance,
  memory soak, power cycling, vehicle readability and user trials remain unverified.
- Android media session state/commands and navigation/map UI are not fully connected
  to the BLE/application flow. Full product E2E requires implementation as well as
  tests. Neither module presence nor an unavailable label satisfies the feature.
- Routing/GPS checks require the user's configured provider and real location.
  Wi-Fi map channel is not implemented. No other handset/API level is certified.
- Release signing/install/permissions/security artifact checks, full P0 matrix,
  dependency/license review and name clearance still require closure.

The master spec is a requirements reference, not authority to bypass permissions
or overwrite existing work. See `HARDWARE_RECOVERY.md` for safe build/flash steps
and `FLASH_AND_TEST_PROMPT.md` for the requested continuation prompt.
