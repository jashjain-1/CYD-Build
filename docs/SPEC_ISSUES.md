# Specification issues

Rule 7 of §24: the specification file is read-only; problems with it are proposed
here. Each entry names the section, what is ambiguous or inconsistent, the reading
this project is implementing, and what would resolve it.

Nothing here blocks work: every item has a defensible default (§0.5).

---

## SI-01 — DEC-03's fallback is to an end-of-support branch

* **Sections:** §3 DEC-03.
* **Issue:** if GATE-03 fails with LVGL 9 + TFT_eSPI, the stated fallback is "LVGL 8.4
  LTS-line". LVGL's own support policy lists 8.4 as **support-ended on 2025-03-20**.
  The fallback therefore trades a UI-stack risk for an unmaintained dependency on a
  device that manages BLE bonds and user data.
* **Reading implemented:** the fallback is the newest working **LVGL 9.x** patch
  release, stepping down within 9.x. The intent of DEC-03 — one display stack, a
  maintained release — is preserved; only the version lineage changes.
* **Resolution needed:** amend DEC-03's fallback wording, or state explicitly that an
  unmaintained branch is acceptable for this product (it is not, in this reading).

## SI-02 — §5.2's touch pin hypothesis is wrong for at least one revision

* **Sections:** §5.2 (expected pin map), §4 GATE-02.
* **Issue:** §5.2 asserts touch is on a separate bus at CLK 25 / MOSI 32 / MISO 39 /
  CS 33 as the "starting hypothesis", with the display bus at 14/13/12. GATE-02
  confirmed exactly that on this board, but only after the *first* reading (shared
  display bus) failed. Other revisions in the wild share one bus. The spec does not
  say which revision is which, and the boards carry no readable revision identifier.
* **Reading implemented:** the pin map is a measured property of the board, held in
  `dashcue_config.h`, and the probe scans rather than assumes. Calibration and route
  data are revision-bound and the revision string travels in the protocol
  (`Hello.hw_revision`).
* **Resolution needed:** none strictly, but §5.2 should stop presenting one revision's
  wiring as "the" expected map without saying which revision it describes.

## SI-03 — §21's "clean-host setup succeeds following README" is unrunnable as written

* **Sections:** §21 (Definition of done), §19.8 TC-E2E-01, §4 GATE-01.
* **Issue:** "a clean-host setup following `README.md` succeeds" is part of the
  definition of done, but there is no defined clean host, no snapshot mechanism and no
  acceptance procedure. On a project with one developer machine, this cannot be
  verified as stated — it can only be asserted.
* **Reading implemented:** the closest verifiable form is used: `tools/setup_check.py`
  reports exactly which prerequisites are present or missing and exits non-zero when a
  required one is missing, so a failure on a genuine clean host is actionable rather
  than mysterious. TC-E2E-01 stays `NOT RUN` until a second host exists.
* **Resolution needed:** define the clean host (OS image, whether the Android SDK
  license is interactive, whether network access is available) or restate the
  criterion.

## SI-04 — §16's flash budget and §18's environment list disagree about panel variants

* **Sections:** §16 (flash ≥ 25 % free), §18 (`cyd_ili9341|cyd_st7789|diag`), §5.1.
* **Issue:** a ST7789 panel needs a different TFT_eSPI driver define, i.e. a different
  firmware binary, not a runtime setting — so "app partition ≥ 25 % free" is a
  property of a specific binary, and the spec does not say which environment's number
  the release must satisfy.
* **Reading implemented:** the budget is measured and reported **per environment**, and
  the release report names the environment it was measured in. Worst case counts.
* **Resolution needed:** say which environment the release budget applies to.

## SI-05 — the driving-lock speed source is documented twice, differently

* **Sections:** §3 DEC-28 (HUD disables features above 8 km/h), §9.4/§9.6 (HEARTBEAT
  carries `speed_kmh_x10`, `-1` unknown), §11.2 (phone computes guidance).
* **Issue:** both the phone and the HUD are described as enforcing the lock, and the
  HUD's only speed source is the heartbeat. When the phone is disconnected the HUD has
  **no** speed and cannot know whether the car is moving; §23 resolves "restrictions
  only when speed known", which means a HUD that loses its link must decide whether to
  re-enable settings entry.
* **Reading implemented:** the HUD treats unknown speed as *not* moving (per §23) but
  carries a stale-speed timer: once the link is stale the speed is unknown, and the
  lock is released while a visible "phone disconnected" banner is shown, so the driver
  is never blocked by a dead link. The phone enforces its own lock independently
  (TC-AN-12).
* **Resolution needed:** state the intended behaviour for the disconnected case
  explicitly; it is a safety-relevant choice, not a detail.

## SI-06 — §19.1's evidence format implies test IDs that the §19 tables never assign

* **Sections:** §19.1, §19.9.
* **Issue:** §19.1 names directories by `TC-ID`, and §19.9 defines `TC-PERF-01`,
  `TC-SOAK-01`, `TC-SEC-01..03`. Test groups that exist in prose without IDs (e.g. the
  "fuzz tests on the reassembler and decoder" in §17, which map to TC-PR-12) have no
  single canonical home; GATE-01's "record in DEPENDENCIES.md" likewise has no TC ID.
* **Reading implemented:** gate evidence is stored under `GATE-xx` directories and test
  evidence under `TC-xx` directories, both inside `docs/evidence/<yyyymmdd>/`, so every
  artefact has a stable ID either way.
* **Resolution needed:** none functionally; noted so the layout is not read as a
  deviation from §19.1.

## SI-07 — the licence of "the project" is never decided

* **Sections:** §18 (a `LICENSE` file in the repo root), §7.4 (third-party licences).
* **Issue:** §18 requires a `LICENSE` file but no section chooses a licence for the
  project itself, and §12.10/§14 require redistributable fonts and original artwork —
  both of which constrain the choice.
* **Reading implemented:** `LICENSE` states that no licence has been granted yet, so
  the repository does not accidentally grant rights the owner has not chosen. This is
  the safe default, not a decision.
* **Resolution needed:** the project owner picks a licence; recorded in
  [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md).
