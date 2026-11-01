# Bug audit — touch, UI and pairing removal

Scope: the CYD HUD firmware (`firmware/`) and the DashCue Android app
(`android/`) at the current `main` working tree. Every entry below names the
evidence used. A finding is only marked `FIXED`/`REGRESSION-PASSED` when a
command in this repository ran and passed after the change.

Verification environment available during the session (evidence levels are separated):

| Environment | Available | Notes |
|---|---|---|
| ESP32 CYD | **Yes** — COM4 was identified during the live session; serial banner reports `cyd-3248s035r-st7796`, 480x320 | User confirms Maps/system works and board produces sound. No captured touch-coordinate matrix or CYD screen image. Serial opening resets the board, so no further probe after user requested a safe stop. |
| Android handset over ADB | **Yes** — one personal handset (ADB serial removed for privacy) | Live Maps screenshot and earlier BLE/media state observations; screenshot is phone-side Google Maps, not HUD glass. |
| Firmware target build | `pio run -e cyd_ili9341` | Build/link evidence only; exact compiled binary was not read back from the live board. |
| Firmware native tests | `pio test -e native` | Hardware-independent sources only. |
| Kotlin compile/package | `./gradlew :app:compileDebugKotlin`, `:app:assembleDebug` | Compilation/package evidence; separate from actual device behavior. |

---

## BUG-01 — Pairing countdown drained 100x too fast

- **Component:** firmware BLE peripheral / application state
- **Files:** `firmware/src/transport/ble_peripheral.cpp`, `firmware/src/state/app_state.cpp`
- **Severity:** High (misleading UI, contradictory state)
- **Reproduction:** Boot the HUD, open More → Bluetooth → Pair, watch the
  "Pairing window: Ns remaining" label. It reached 0 in roughly 1.2 s while the
  window was still open for 120 s.
- **Expected:** the displayed countdown matches the real 120 s window.
- **Actual:** the counter reached zero almost immediately; `app_state` then
  reported `is_pairing_window_active == false` while Bluedroid was still
  advertising in pairing mode.
- **Root cause:** `ble_peripheral_tick()` was called once per protocol-loop
  iteration (~10 ms) and unconditionally called
  `app_state_tick_pairing_window()`, which decrements a *seconds* counter by one
  per call. The countdown was therefore driven by tick count, not elapsed time.
- **Fix applied:** the countdown is advanced once per real elapsed second
  (`while (now - last_sec_ms >= 1000)`) and the window-expiry check is evaluated
  before the decrement.
- **Regression test:** `tests/adversarial/test_touch_hitbox_geometry.py` plus the
  firmware build; the window/passkey code has since been removed entirely by
  BUG-04, so the remaining regression is "no seconds counter is driven by loop
  count" — verified by the absence of the decrement path (grep) and a clean
  `pio run -e cyd_ili9341`.
- **Status:** `FIXED`, then superseded by `BUG-04`.

## BUG-02 — Pairing countdown label never refreshed

- **Component:** firmware UI manager / pairing screen
- **Files:** `firmware/src/ui/ui_manager.cpp`, `firmware/src/ui/ui_screen_pairing.cpp`
- **Severity:** Medium
- **Reproduction:** Start a pairing window and stay on the pairing screen. The
  label keeps the value from the `IPC_EVT_PASSKEY` message; it never changes and
  the screen never leaves when the window expires.
- **Expected:** a live countdown, and the screen dismisses when the window ends.
- **Actual:** the label was frozen at its initial value and the screen stayed up
  after expiry (the BLE layer had already stopped advertising).
- **Root cause:** `UiScreenPairing::setRemainingSeconds()` had exactly one caller,
  the passkey event; no periodic refresh existed and no expiry transition was
  wired.
- **Fix applied:** the UI refresh polls `app_state_get_pairing_remaining_s()`
  while the pairing screen is visible and navigates away when the window is no
  longer active; `setRemainingSeconds()` skips the repaint when the whole-second
  value is unchanged.
- **Status:** `FIXED`, then superseded by `BUG-04` (the screen no longer exists).

## BUG-03 — Modal confirm overlay did not block taps (hit-through)

- **Component:** firmware UI, navigation screen
- **Files:** `firmware/src/ui/ui_screen_nav.cpp`, `firmware/src/ui/ui_screen_nav.h`
- **Severity:** High (destructive action reachable through a "blocking" dialog)
- **Reproduction:** On the navigation screen, tap **End Route** to open the
  confirmation dialog, then tap the dimmed area outside the dialog box where the
  **Launch Maps** / **End Route** button sits underneath.
- **Expected:** the scrim absorbs the tap; nothing under it activates.
- **Actual:** the tap reached the control underneath the dialog.
- **Root cause:** the scrim was created with `lv_obj_create()` and never given
  `LV_OBJ_FLAG_CLICKABLE`. In the pinned LVGL 9.6.0,
  `lv_obj_hit_test()` returns `false` for a non-clickable object
  (`src/core/lv_obj_pos.c:1236`), so `lv_indev_search_obj()` skipped the scrim
  and continued to the next sibling.
- **Fix applied:** the overlay claims the hit area
  (`lv_obj_add_flag(m_confirm_modal, LV_OBJ_FLAG_CLICKABLE)`) and has an
  explicitly inert `CLICKED` handler, so taps are consumed rather than ignored.
  Its children (the two dialog buttons) are still searched first, so they stay
  reachable.
- **Regression test:** `test_modal_scrim_blocks_underlying_controls`,
  `test_modal_buttons_are_reachable_through_scrim`,
  `test_modal_ext_click_area_still_resolves_to_its_own_button` and
  `test_modal_buttons_do_not_overlap_each_other` in
  `tests/adversarial/test_touch_hitbox_geometry.py`; plus Rule 6 of
  `tools/audit_ui_screens.py`, which fails any full-screen `*modal*`/`*scrim*`/
  `*overlay*` object that is not CLICKABLE.
- **Status:** `REGRESSION-PASSED` (model + static audit). Physical tap-through
  test is `BLOCKED` (no panel attached).

## BUG-04 — BLE pairing requirement removed

- **Component:** firmware BLE transport, IPC, UI; Android BLE
- **Severity:** N/A (requested behaviour change)
- **Change:** the HUD no longer requires a BLE pairing/bonding step. Removed:
  `ESP_LE_AUTH_REQ_SC_MITM_BOND` + DisplayOnly passkey (GAP handler deleted),
  MITM/ENC attribute permissions (RX/TX/CCCD are now unauthenticated), the
  pairing window and its countdown, the passkey/dismiss IPC events,
  `IPC_CMD_PAIR_PHONE`/`IPC_CMD_CANCEL_PAIRING`, `ui_screen_pairing.{h,cpp}`,
  `UI_SCREEN_PAIRING`, the Settings **Pair** button, the nav screen's
  "Start pairing" button, and the Android Companion Device Manager association
  flow (`CompanionDeviceHelper.kt`, `requestCdmPairing()`).
- **Behaviour now:** the HUD advertises the DashCue service continuously while
  disconnected; the app scans and connects with no chooser, no passkey and no
  system pairing dialog. The Android connect timeout dropped from 120 s
  (waiting for passkey entry) to 20 s.
- **Security consequence:** any nearby BLE central can read notifications and
  write frames. The only remaining validation boundary is the protocol layer
  (frame length, CRC32, fragment bounds, dedupe, protobuf field validation).
  This is recorded in [the threat model](../security/threat-model.md); it is an
  accepted product decision, not an oversight.
- **Regression test:** `pio run -e cyd_ili9341` (build), `pio test -e native`
  (24/24), `./gradlew :app:compileDebugKotlin` (compiles), and a grep gate for
  the removed symbols (see "Removed-symbol gate" below).
- **Status:** `FIXED` in source. The live session observed Android Maps/media state over BLE and the user confirms Maps/system operation on the CYD. This is partial hardware E2E evidence, not a recorded protocol trace or a complete reconnection/security matrix; see [E2E results](e2e-results.md).

## BUG-05 — UI audit enforced duplicate click events

- **Component:** test tooling
- **Files:** `tools/audit_ui_screens.py`,
  `tests/adversarial/test_ui_discipline.py`,
  `tests/adversarial/test_ui_touch_sync_empirical.py`
- **Severity:** Medium (would have introduced double actions if followed)
- **Reproduction:** `python tools/audit_ui_screens.py` before this change — it
  failed every screen for not registering both `LV_EVENT_CLICKED` and
  `LV_EVENT_SHORT_CLICKED`.
- **Expected:** a single activation event per control.
- **Actual:** the audit demanded both. In LVGL 9 one ordinary tap emits
  `RELEASED` → `SHORT_CLICKED` → `CLICKED`
  (`src/indev/lv_indev.c`, `indev_proc_short_click()`), so subscribing to both
  runs the handler twice; only the ad-hoc 300 ms `static` debounce inside each
  handler prevented a visible duplicate action, and that debounce also swallowed
  legitimate rapid double taps.
- **Root cause:** an earlier generated audit rule encoded the misconception that
  resistive panels need both events to capture a release.
- **Fix applied:** Rule 3 is now "single activation event": a file fails if any
  `lv_obj_add_event_cb(..., LV_EVENT_SHORT_CLICKED, ...)` exists. Rule 2 no
  longer flags `lv_obj_set_ext_click_area(x, 0)`, which is deliberate for rows of
  adjacent controls, and instead flags the half-measure range 1–7 px.
- **Regression test:** the two adversarial test modules (11 passed) and the
  audit itself (exit 0, all screens).
- **Status:** `REGRESSION-PASSED`.

## BUG-06 — Touch hitboxes were never actually inventoried

- **Component:** test tooling
- **Severity:** Medium (no coverage for the defect class users hit most)
- **Gap:** nothing in the repository asserted that a control's drawn geometry is
  reachable, that adjacent hitboxes do not overlap, or that boundaries follow one
  convention.
- **Fix applied:** `tests/adversarial/test_touch_hitbox_geometry.py` models
  LVGL's own hit test (half-open rectangles, newest-first child search,
  non-clickable objects skipped, hidden objects skipped) and asserts, for the
  tab bar, media transport row and navigation dialog: center of every control,
  all four corners, points one pixel outside, shared boundaries between adjacent
  controls, disabled controls, out-of-range and negative coordinates, and the
  12 px gutter between the dialog buttons.
- **Anti-drift guard:** `test_inventory_matches_source` re-reads the firmware
  sources and fails if the dimensions transcribed into the inventory no longer
  appear there, so the inventory cannot rot into fiction.
- **Status:** `REGRESSION-PASSED` (19 tests). Absolute-coordinate assertions for
  flex-laid-out Settings/SafeMode rows are not claimed: those controls are
  covered by the static audit's size/flag rules only.

## BUG-07 — Product environment name is misleading (panel profile itself matches)

- **Component:** firmware build configuration
- **Files:** `firmware/platformio.ini`, `firmware/include/dashcue_config.h`, `boards/cyd_2432s028r.json`
- **Severity:** Informational (build-selection confusion, not a demonstrated runtime defect)
- **Finding:** the product environment is named `cyd_ili9341`, but its compiler
  flags select `ST7796_DRIVER`; the live board banner reports
  `cyd-3248s035r-st7796`, 480x320. The hardware revision and selected panel
  driver agree with one another; the board JSON is a generic 2.8-inch board
  definition and is not adequate identification of this 3.5-inch unit.
- **Impact:** users selecting by environment name can infer the wrong controller.
- **Fix/status:** no rename was made because existing build/flash scripts target
  `cyd_ili9341`; document the actual ST7796 profile and keep the current build
  invocation. Consider a compatibility alias migration separately. Board-side
  color/rotation validation remains `UNVERIFIED` because no display capture was
  saved.

---

## Phone-side interaction checks (ADB, handset serial redacted)

Performed on the installed debug build; details and the exact commands are in
`docs/testing/e2e-results.md`. Statuses below refer to those runs only.

| Check | Result |
|---|---|
| App installs and launches without crash | see e2e-results |
| Connect button enabled/disabled state matches link state | see e2e-results |
| Connect no longer opens a system pairing chooser | see e2e-results |
| Back navigation and tab switching | see e2e-results |
| Duplicate request suppression on rapid taps | see e2e-results |

## Out of scope / not claimed

- User confirms Maps and device operation, and audible output from the board; the retained session evidence is not a full packet capture or controlled audio test.
- No raw touch calibration / corner / edge matrix or CYD display screenshot was recorded. Physical hitbox accuracy is `BLOCKED` for evidence, not assumed from the model tests.
- No exact firmware hash was read from the connected board; compiled target and flashed image identity are not proven identical.
- Pairing removal makes the BLE service unauthenticated. Unauthorized-peer testing and the full reconnect/process-death/security matrix were not performed.
- Android screenshots are handset captures. The Maps screenshot shows Google Maps on the phone, not pixels on the CYD.
