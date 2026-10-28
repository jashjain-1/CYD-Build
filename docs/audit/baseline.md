# Baseline — 2026-10-28

## Scope and environment

Captured before edits on `main`; the checkout was initially clean. Environment: Windows 11, Python 3.11.9, OpenJDK 17.0.20.1, PlatformIO Core 6.2.0, pinned `espressif32@7.1.3`, Android SDK 36 / target SDK 36, Gradle wrapper. One physical OnePlus CPH2585 (Android 16/API 36) is connected over ADB, and one ESP32-2432S028R board is enumerated as COM4. Physical availability does not imply that a device workflow was run.

A prior, timestamped host-simulation result already existed at [result.json](../evidence/20261028/host-simulation/154519-403752Z/result.json); it is retained and explicitly says Android/hardware were not exercised. Outputs created by these baseline commands should be regenerated rather than treated as current-release evidence.

## Commands and results

| Command | Status | Observed result / scope |
|---|---|---|
| `python tests/e2e/run_e2e_tests.py --tier all -v` | PASS | 265/265 host-model/source/literal-assertion cases (115 + 115 + 23 + 12). Runner explicitly labels Android and physical E2E NOT RUN. |
| `python -m pytest tests/adversarial/ -v` | PASS | Adversarial host tests passed; this does not establish radio/device behavior. |
| `pio test -e native -v` | PASS | 24/24 Unity tests on host, exercising shared hardware-independent firmware logic. |
| `./gradlew lintDebug assembleDebug --no-daemon -Dkotlin.compiler.execution.strategy=in-process` | FAIL | Lint returned 2 errors and 6 warnings. Errors were MissingPermission at `CompanionDeviceHelper.kt:100,111` for companion presence-observation methods. Warnings include a wake-lock without timeout and obsolete API guards. |
| Android unit test suite (existing Gradle reports) | PASS | 22 reports; 138 tests, 0 failures/errors/skips. Baseline test suite invoked previously; report totals were independently summed from XML. |
| `./gradlew assembleRelease --no-daemon ...` | PASS | Release APK assembled. This alone is not a release sign-off. |
| `pio run -e cyd_ili9341` | PASS | Product firmware compiled for the pinned Arduino-ESP32/Bluedroid target. No firmware was flashed as part of the baseline command. |
| `python tools/contrast_check.py` | PASS | Palette contrast checks passed. |
| `python tools/check_dependency_pins.py` | FAIL | The baseline gate failed; inspect its captured console output before using it as a release gate. No claim of dependency drift diagnosis is made here. |
| `python tools/gen_lv_conf.py --check` | FAIL | `firmware/include/lv_conf.h is out of date`; generation from the pinned LVGL 9.6.0 template reproduced the config during remediation. |
| `adb devices -l` | PASS (inventory only) | One authorized physical handset visible: CPH2585, Android 16, API 36. App install was not established by this query. |
| `pio device list` | PASS (inventory only) | CH340 serial adapter enumerated as COM4. No firmware flashing or pairing is implied. |
| BLE connection/pairing/Android-to-HUD E2E | NOT RUN | No pairing/session trace was captured during baseline. The earlier host tests use simulation/model boundaries. |
| Performance before/after measurements | NOT RUN | No comparable latency/throughput measurement was collected in this audit. Existing board measurements are historical in `docs/HARDWARE_REPORT.md`. |

## Findings exposed by baseline/source cross-check

1. Firmware's source selected bond-only `ESP_LE_AUTH_BOND`, IO capability `ESP_IO_CAP_NONE`, and plaintext GATT permissions on RX and TX despite protocol/UUID documentation requiring authenticated encryption. Fixed in the audit change; see [findings](findings.md).
2. Firmware advertises `DASHCUE_MAX_PAYLOAD_SIZE=2048` in `HELLO_ACK`; Android previously encoded/decoded 4096-byte payloads. Aligned to the firmware contract and added boundary coverage.
3. Android's source framing differs from stale protocol text: actual envelope is a 16-byte header (`magic/version/flags/type/msg_id/length/reserved`) and the fragment header is 4 bytes (`lane/count/index/credits`); old prose described a different protocol.
4. The LVGL generated configuration had drifted; generator output and a `--check` verification are part of remediation.

## Reproduction

From repository root: run the exact commands in the table. For target firmware, `cd firmware && pio run -e cyd_ili9341`; native tests: `cd firmware && pio test -e native -v`; Android: `cd android && ./gradlew testDebugUnitTest lintDebug assembleDebug assembleRelease --no-daemon -Dkotlin.compiler.execution.strategy=in-process`. Host simulation: `python tests/e2e/run_e2e_tests.py --tier all -v`. Commands can create build/evidence outputs; inspect `git status` afterward.
