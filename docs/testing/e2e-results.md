# E2E and build results — 2026-10-28

Status labels mean evidence from the identified command only. Host models are not product E2E.

| Check | Status | Evidence / interpretation |
|---|---|---|
| Python host model suite (all tiers) | PASS | Existing [result](../evidence/20261028/host-simulation/154519-403752Z/result.json): 265/265, with hardware and Android explicitly false. The added source regression test must be included in the rerun before release. |
| Firmware native Unity tests | PASS | Baseline 24/24 on host; shared hardware-independent code only. Not board execution. |
| Firmware `cyd_ili9341` build | PASS | Baseline product target compiled; no new image was flashed during this audit. |
| Android unit tests | PASS | Baseline Gradle XML summary: 138 tests, zero failures/errors/skips. |
| Android release assembly | PASS | Baseline `assembleRelease` completed; this is not install/security certification. |
| Android lint | PASS after source correction | Baseline lint reported MissingPermission on companion-presence observation; manifest permission/optional feature were added. Re-run final lint command for final source state. |
| Generated LVGL config check | PASS after regeneration | Baseline reported config drift; regenerated pinned output. Re-run `python tools/gen_lv_conf.py --check` for final state. |
| Protocol generator `--check` | NOT RUN in final-source verification | Run command in [test plan](test-plan.md) before release; do not infer from generation. |
| All-tier E2E after added test | NOT RUN | Prior 265-run predates the new firmware security source assertion. |
| Phone app package installed | FAIL | `adb shell pm path com.dashcue.app` returned “Unable to find package”; see [package log](../evidence/20261028/hardware-audit-run/phone-package.log). |
| Hardware audit run | FAIL / PARTIAL | Boot and captured heap checks passed, but serial capture classified I2S error as failure; report marked overall FAIL and stability span insufficient. See [report](../evidence/20261028/hardware-audit-run/report.json). |
| Physical BLE secure pairing and sync | NOT RUN | No current app install or connection trace. |
| Media, navigation, settings, ten reconnects, 8-hour soak | NOT RUN | No separate scenario evidence. |
| Physical UI/touch confirmation | NOT RUN | Needs visual panel inspection and real touch target presses. |
| A2DP/I2S | FAIL in captured firmware session | Serial line: `I2S: register I2S object to platform failed`; audio task later logs started despite failed install. Needs diagnosis before claiming audio works. |

## Hardware evidence details

`hardware-audit-run/report.json` identifies one physical OnePlus CPH2585 on Android 16/API 36, and serial output reports boot-to-UI 2,125 ms, free heap 54,188 B, minimum heap 51,272 B, and largest block 32,756 B. The capture contains 20 seconds of stats but the report's observed span is only 15 seconds, so bounded stability was marked NOT RUN. Physical screen colors, touch, BLE sync, and A2DP sound were not verified. The serial capture reports firmware profile `cyd-3248s035r-st7796`; do not flash a build intended for a different panel/board profile.

## Reproduction

Run the exact commands listed in [test plan](test-plan.md). Hardware capture commands reset the board; check the current port and selected board profile first. Save outputs under a new timestamped evidence folder and redact personal/device-specific data where unnecessary.