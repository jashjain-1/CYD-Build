# Release checklist

Run this top to bottom before any public release. Every line is either an action with
an evidence artefact, or a decision that only the project owner can make.

Current state: **M0 complete, M1 in progress.** This checklist cannot pass yet, and the
unfinished items say why rather than being silently omitted.

---

## 0. Decisions the owner must make

| # | Decision | State |
|---|---|---|
| 0.1 | **Trademark/name availability for "DashCue"** — required by §2.1 before any public release. Search the name in the relevant trademark registers and app stores, and look for an existing product in the same category. | **NOT DONE.** Record the date, the registers searched and the outcome below. |
| 0.2 | **Project licence.** §18 requires a `LICENSE` file and no section chooses one. `LICENSE` currently grants nothing, which is the safe default. Note that the UI font and any bundled icons must be redistributable. | **OPEN** |
| 0.3 | Attribution and terms for the map style, tile source and routing host actually configured. | **OPEN** (blocks M6/M10) |

Trademark search result: _not yet performed_ — record outcome, date and sources here.

## 1. Definition of done (§21)

- [ ] Every **P0** test in §19 has `PASS` evidence in `docs/evidence/<date>/<TC-ID>/`.
- [ ] Every **P1** failure has a written waiver (who accepted it, why, impact).
- [ ] `docs/KNOWN_ISSUES.md` is complete and accurate.
- [ ] `app-release.apk` installs with `adb install -r` on the tested handset.
- [ ] Firmware flashes with one command (`python tools/flash_fw.py`).
- [ ] A clean-host setup following `README.md` succeeds (§21; see SI-03 for why this
      needs a second host to be verifiable rather than assertable).

## 2. Build and artefact checks

- [ ] Versions consistent across firmware, app and protocol
      (`tools/evidence_report.py`, `tools/check_dependency_pins.py`).
- [ ] Flash headroom ≥ 25 % in the shipping environment, measured with
      `pio run -e <env> -t size`, recorded per environment (SI-04).
- [ ] Release APK: R8/ProGuard on, `debuggable=false`, debug signing config absent.
- [ ] No test doubles, mocks, diagnostic console or fake data paths in the release
      artefacts (`tools/check_release_scan.py`; DEC-26/27).
- [ ] No secrets: keystore, `keystore.properties`, `local.properties`, endpoint
      credentials are all git-ignored and absent from the artefacts.
- [ ] Manifest permissions match [PERMISSIONS.md](PERMISSIONS.md) exactly (TC-SEC-02).
- [ ] Licence and attribution files present; the in-app licences screen lists every
      provider and font.
- [ ] No `TODO`, `FIXME`, stub or dead module in the release tree (§18).

## 3. Hardware and firmware

- [ ] `docs/HARDWARE_REPORT.md` complete for the board being released.
- [ ] Touch calibration valid and stored; the uncalibrated-safety path verified
      (TC-HW-04).
- [ ] 100 power cycles with random timing — boots every time, settings and calibration
      intact (TC-HW-12).
- [ ] Fault injection: queue overflow, corrupt settings section, watchdog, repeated
      crash → safe mode (TC-HW-11).
- [ ] Heap targets after an 8 h soak: min free ≥ 40 KB, largest block ≥ 20 KB, drift
      ≤ 2 KB.

## 4. Phone side

- [ ] `docs/ANDROID_COMPAT.md` filled in for each tested handset and OS version, with
      observed OEM restrictions.
- [ ] Media compatibility matrix (`docs/MEDIA_COMPAT.md`) generated from test results;
      untested apps stay `UNTESTED`.
- [ ] Onboarding verified on a **fresh install**, including each denied-permission path
      and the Android 13+ restricted-settings case.
- [ ] Background survival measured and documented per device (GATE-09).
- [ ] Notification-listener scope reviewed: media sessions only.

## 5. Honest-claims review

Read the README, the app UI strings and `docs/` and confirm:

- [ ] No claim of live traffic, speed limits, lane guidance or voice prompts.
- [ ] ETA is labelled "estimate, no live traffic" everywhere it appears.
- [ ] The HUD map is described as schematic wherever a user could assume otherwise.
- [ ] No feature is marked "supported" without a passing test (§6 Rule 16).
- [ ] Absent capabilities read as absent, not as broken: every disabled control says
      why.
- [ ] The declared limitations in §22 are present in the README and the app.
- [ ] No regulatory or safety certification is implied.

## 6. Sign-off

| Role | Name | Date | Notes |
|---|---|---|---|
| Build and tests | | | |
| Hardware | | | |
| Phone app | | | |
| Final release decision | | | |
