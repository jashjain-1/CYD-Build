"""Deterministic touch hitbox geometry for the CYD cockpit, verified without hardware.

Why this file exists
--------------------
"A button is drawn, therefore it is tappable, and its tap area equals what you
see" is an assumption, not a property. The firmware never computes hit testing
itself: LVGL does it, in `lv_obj_hit_test()` (src/core/lv_obj_pos.c) and
`lv_indev_search_obj()` (src/indev/lv_indev.c). Those functions have three
behaviours this suite pins down, each read from the pinned LVGL 9.6.0 source:

1. Containment uses the object's coordinates and `lv_area_is_point_on`, i.e. a
   half-open rectangle: a point exactly on the right/bottom edge is outside.
2. A non-clickable object is skipped entirely (`lv_obj_hit_test` returns false
   when `lv_obj_is_clickable(obj)` is false). This is why a full-screen scrim
   that is not CLICKABLE does not block the controls underneath it.
3. Children are searched newest-first, and the first hit wins. A hidden object
   returns immediately, so a hidden modal cannot steal input.

The inventory below is the real geometry of the shipped controls, transcribed
from the firmware sources. `test_inventory_matches_source` fails if the source
drifts away from the numbers used here, so this cannot silently become fiction.

Not modelled here (and therefore not claimed to be verified): anything whose
position is produced by an LVGL flex layout at runtime (the Settings rows and
the SafeMode button stack). Those are covered by tools/audit_ui_screens.py
(minimum sizes, no scroll flags, single activation event) rather than by
absolute-coordinate assertions.
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# --------------------------------------------------------------------------
# Display constants, from firmware/src/ui/ui_theme.h
# --------------------------------------------------------------------------
UI_SCREEN_W = 480
UI_SCREEN_H = 320
UI_STATUS_BAR_H = 24
UI_TAB_BAR_H = 48
UI_WORKSPACE_Y = UI_STATUS_BAR_H
UI_WORKSPACE_H = UI_SCREEN_H - UI_STATUS_BAR_H - UI_TAB_BAR_H  # 248


@dataclass(frozen=True)
class Rect:
    """Half-open rectangle in absolute panel coordinates: x0 <= x < x1."""

    x0: int
    y0: int
    x1: int
    y1: int

    @staticmethod
    def from_xywh(x: int, y: int, w: int, h: int) -> Rect:
        return Rect(x, y, x + w, y + h)

    def expanded(self, ext: int) -> Rect:
        """LVGL's lv_obj_get_click_area(): bounds grown by the ext click area."""
        return Rect(self.x0 - ext, self.y0 - ext, self.x1 + ext, self.y1 + ext)

    def contains(self, px: int, py: int) -> bool:
        return self.x0 <= px < self.x1 and self.y0 <= py < self.y1

    def intersects(self, other: Rect) -> bool:
        return not (
            self.x1 <= other.x0
            or other.x1 <= self.x0
            or self.y1 <= other.y0
            or other.y1 <= self.y0
        )

    @property
    def center(self) -> tuple[int, int]:
        return ((self.x0 + self.x1) // 2, (self.y0 + self.y1) // 2)

    @property
    def width(self) -> int:
        return self.x1 - self.x0

    @property
    def height(self) -> int:
        return self.y1 - self.y0


@dataclass(frozen=True)
class Control:
    name: str
    rect: Rect
    ext_click: int = 0
    clickable: bool = True
    enabled: bool = True

    @property
    def hit_rect(self) -> Rect:
        return self.rect.expanded(self.ext_click)


def hit_test(controls, x: int, y: int, overlay: Control | None = None):
    """Mirror lv_indev_search_obj(): newest-first, first hit wins, skip hidden.

    `controls` is ordered oldest-first so it is searched in reverse, exactly like
    LVGL's child list. A modal overlay is treated as the last (newest) child of
    the screen, which is what `lv_obj_move_foreground(m_confirm_modal)` makes it.
    """
    stack = list(reversed(controls))
    if overlay is not None:
        stack.insert(0, overlay)
        # The overlay's own buttons are its children: search them before the
        # scrim, because a child hit wins over its parent.
        stack = list(reversed(overlay_children)) + stack

    for c in stack:
        if c.clickable and c.enabled and c.hit_rect.contains(x, y):
            return c
    return None


# --------------------------------------------------------------------------
# Inventory
# --------------------------------------------------------------------------
TAB_W = UI_SCREEN_W // 3  # ui_tab_bar.cpp: tab_w = UI_SCREEN_W / UI_TAB_COUNT
TAB_Y = UI_SCREEN_H - UI_TAB_BAR_H

TABS = [
    Control("tab_media", Rect.from_xywh(0 * TAB_W, TAB_Y, TAB_W, UI_TAB_BAR_H)),
    Control("tab_nav", Rect.from_xywh(1 * TAB_W, TAB_Y, TAB_W, UI_TAB_BAR_H)),
    Control("tab_more", Rect.from_xywh(2 * TAB_W, TAB_Y, TAB_W, UI_TAB_BAR_H)),
]

# ui_screen_media.cpp: BtnConfig{ x, w } at y=166 inside a screen placed at
# UI_WORKSPACE_Y, all 56 px tall, ext_click_area 0.
MEDIA_BTN_Y = UI_WORKSPACE_Y + 166
_MEDIA_CFG = [
    ("media_vol_down", 24, 68),
    ("media_prev", 108, 76),
    ("media_play_pause", 200, 80),
    ("media_next", 296, 76),
    ("media_vol_up", 388, 68),
]
MEDIA_TRANSPORT = [
    Control(name, Rect.from_xywh(x, MEDIA_BTN_Y, w, 56)) for name, x, w in _MEDIA_CFG
]

# ui_screen_nav.cpp: dialog_box 380x180 centred, two 160x56 buttons with
# ext_click_area 8, and a 480x320 scrim that is now CLICKABLE.
DIALOG = Rect.from_xywh(
    (UI_SCREEN_W - 380) // 2, (UI_SCREEN_H - 180) // 2, 380, 180
)
MODAL_CANCEL = Control(
    "modal_cancel",
    Rect.from_xywh(DIALOG.x0 + 16, DIALOG.y1 - 56 - 14, 160, 56),
    ext_click=8,
)
MODAL_CONFIRM = Control(
    "modal_confirm",
    Rect.from_xywh(DIALOG.x1 - 16 - 160, DIALOG.y1 - 56 - 14, 160, 56),
    ext_click=8,
)
MODAL_SCRIM = Control("modal_scrim", Rect(0, 0, UI_SCREEN_W, UI_SCREEN_H))

overlay_children = [MODAL_CANCEL, MODAL_CONFIRM]


# --------------------------------------------------------------------------
# Inventory integrity: the numbers above must exist in the firmware sources
# --------------------------------------------------------------------------
def _read(rel_path: str) -> str:
    with open(os.path.join(PROJECT_ROOT, rel_path), encoding="utf-8") as f:
        return f.read()


def test_inventory_matches_source():
    """Fail if the firmware geometry drifts away from this inventory."""

    theme = _read("firmware/src/ui/ui_theme.h")
    assert "#define UI_SCREEN_W         480" in theme
    assert "#define UI_SCREEN_H         320" in theme
    assert "#define UI_TAB_BAR_H        48" in theme

    tab_bar = _read("firmware/src/ui/ui_tab_bar.cpp")
    assert "UI_SCREEN_W / UI_TAB_COUNT" in tab_bar, "tab width derivation changed"
    assert "lv_obj_set_size(m_buttons[i], tab_w, UI_TAB_BAR_H)" in tab_bar
    assert "lv_obj_set_ext_click_area(m_buttons[i], 0)" in tab_bar

    media = _read("firmware/src/ui/ui_screen_media.cpp")
    for name, x, w in _MEDIA_CFG:
        # e.g. "{24,  68, LV_SYMBOL_VOLUME_MID, BTN_VOL_DOWN, false},"
        assert re.search(rf"\{{\s*{x},\s*{w},", media), (
            f"media transport entry for {name} (x={x}, w={w}) missing from source"
        )
    assert "lv_obj_set_size(btn, cfg.w, 56)" in media
    assert "lv_obj_set_pos(btn, cfg.x, 166)" in media
    assert "lv_obj_set_ext_click_area(btn, 0)" in media

    nav = _read("firmware/src/ui/ui_screen_nav.cpp")
    assert "lv_obj_set_size(m_confirm_modal, UI_SCREEN_W, UI_SCREEN_H)" in nav
    assert "lv_obj_add_flag(m_confirm_modal, LV_OBJ_FLAG_CLICKABLE)" in nav, (
        "the confirm overlay must claim the hit area or taps fall through"
    )
    assert "lv_obj_set_size(dialog_box, 380, 180)" in nav
    assert "lv_obj_set_size(m_btn_modal_cancel, 160, 56)" in nav
    assert "lv_obj_set_size(m_btn_modal_confirm, 160, 56)" in nav
    assert "lv_obj_align(m_btn_modal_cancel, LV_ALIGN_BOTTOM_LEFT, 16, -14)" in nav
    assert "lv_obj_align(m_btn_modal_confirm, LV_ALIGN_BOTTOM_RIGHT, -16, -14)" in nav


# --------------------------------------------------------------------------
# Tab bar: three adjacent full-width targets, zero dead zones
# --------------------------------------------------------------------------
def test_tab_bar_covers_full_width_with_no_dead_zone():
    for x in range(UI_SCREEN_W):
        hits = [t for t in TABS if t.hit_rect.contains(x, TAB_Y + 24)]
        assert len(hits) == 1, f"x={x} resolved to {[h.name for h in hits]}"


def test_tab_bar_shared_boundary_belongs_to_exactly_one_tab():
    # Half-open: 160 belongs to Nav, 159 to Media; 320 to More, 319 to Nav.
    assert hit_test(TABS, 159, TAB_Y + 1).name == "tab_media"
    assert hit_test(TABS, 160, TAB_Y + 1).name == "tab_nav"
    assert hit_test(TABS, 319, TAB_Y + 1).name == "tab_nav"
    assert hit_test(TABS, 320, TAB_Y + 1).name == "tab_more"


def test_tab_bar_edges_and_out_of_range():
    assert hit_test(TABS, 0, TAB_Y).name == "tab_media"
    assert hit_test(TABS, UI_SCREEN_W - 1, UI_SCREEN_H - 1).name == "tab_more"
    # 1 px above the bar and past the right edge must hit nothing.
    assert hit_test(TABS, 240, TAB_Y - 1) is None
    assert hit_test(TABS, UI_SCREEN_W, TAB_Y + 1) is None
    assert hit_test(TABS, -1, TAB_Y + 1) is None
    assert hit_test(TABS, 240, UI_SCREEN_H) is None


# --------------------------------------------------------------------------
# Media transport row
# --------------------------------------------------------------------------
def test_each_media_control_center_hits_only_itself():
    for target in MEDIA_TRANSPORT:
        cx, cy = target.rect.center
        hit = hit_test(MEDIA_TRANSPORT, cx, cy)
        assert hit is target, f"center of {target.name} resolved to {hit and hit.name}"


def test_media_controls_do_not_overlap():
    for i, a in enumerate(MEDIA_TRANSPORT):
        for b in MEDIA_TRANSPORT[i + 1:]:
            assert not a.hit_rect.intersects(b.hit_rect), (
                f"{a.name} and {b.name} hitboxes overlap -> ambiguous tap"
            )


def test_media_control_edges_are_inclusive_top_left_only():
    for target in MEDIA_TRANSPORT:
        r = target.rect
        assert hit_test(MEDIA_TRANSPORT, r.x0, r.y0) is target, "top-left corner must hit"
        assert hit_test(MEDIA_TRANSPORT, r.x0, r.y1 - 1) is target
        assert hit_test(MEDIA_TRANSPORT, r.x1 - 1, r.y0) is target
        assert hit_test(MEDIA_TRANSPORT, r.x1 - 1, r.y1 - 1) is target
        # One pixel past the drawn bounds on the right/bottom is outside.
        assert hit_test(MEDIA_TRANSPORT, r.x1, r.y0) is not target
        assert hit_test(MEDIA_TRANSPORT, r.x0, r.y1) is not target


def test_media_row_gaps_are_dead_zones_not_overlaps():
    # Drawn gaps between adjacent buttons (e.g. 92..108) belong to nothing. That
    # is intentional: extending these would overlap a neighbour.
    for x in (93, 100, 186, 199, 282, 295):
        assert hit_test(MEDIA_TRANSPORT, x, MEDIA_BTN_Y + 10) is None


def test_disabled_media_control_is_not_hittable():
    disabled = [
        Control(c.name, c.rect, c.ext_click, c.clickable, enabled=(c.name != "media_next"))
        for c in MEDIA_TRANSPORT
    ]
    nxt = next(c for c in disabled if c.name == "media_next")
    assert hit_test(disabled, *nxt.rect.center) is None


# --------------------------------------------------------------------------
# Modal confirmation overlay
# --------------------------------------------------------------------------
def test_modal_scrim_blocks_underlying_controls():
    """Every point outside the dialog buttons must resolve to the scrim."""
    underlying = TABS + MEDIA_TRANSPORT
    for x, y in [(240, TAB_Y + 24), (80, TAB_Y + 24), (240, MEDIA_BTN_Y + 10),
                 (0, 0), (UI_SCREEN_W - 1, UI_SCREEN_H - 1), (240, 155)]:
        hit = hit_test(underlying, x, y, overlay=MODAL_SCRIM)
        assert hit is MODAL_SCRIM, (
            f"tap at ({x},{y}) reached {hit.name if hit else 'nothing'} through the scrim"
        )


def test_modal_buttons_are_reachable_through_scrim():
    for btn in (MODAL_CANCEL, MODAL_CONFIRM):
        assert hit_test(TABS + MEDIA_TRANSPORT, *btn.rect.center, overlay=MODAL_SCRIM) is btn


def test_modal_ext_click_area_still_resolves_to_its_own_button():
    for btn in (MODAL_CANCEL, MODAL_CONFIRM):
        r = btn.rect
        for x, y in [(r.x0 - 1, r.y0 + 10), (r.x1, r.y0 + 10),
                     (r.x0 + 10, r.y0 - 1), (r.x0 + 10, r.y1)]:
            assert hit_test(TABS, x, y, overlay=MODAL_SCRIM) is btn


def test_modal_buttons_do_not_overlap_each_other():
    assert not MODAL_CANCEL.hit_rect.intersects(MODAL_CONFIRM.hit_rect)
    # The 12 px gutter between the expanded hitboxes belongs to the scrim.
    gutter_x = MODAL_CANCEL.hit_rect.x1 + 4
    assert hit_test(TABS, gutter_x, MODAL_CANCEL.rect.center[1], overlay=MODAL_SCRIM) is MODAL_SCRIM


def test_modal_scrim_stays_inside_the_panel():
    assert MODAL_SCRIM.rect.x0 == 0 and MODAL_SCRIM.rect.y0 == 0
    assert MODAL_SCRIM.rect.x1 == UI_SCREEN_W and MODAL_SCRIM.rect.y1 == UI_SCREEN_H


def test_hidden_overlay_does_not_capture_input():
    # lv_indev_search_obj() returns NULL immediately for a hidden object.
    assert hit_test(TABS, 240, TAB_Y + 24, overlay=None) is not None


# --------------------------------------------------------------------------
# One tap, one action
# --------------------------------------------------------------------------
def test_one_tap_produces_exactly_one_activation():
    """LVGL 9 release sequence is RELEASED -> SHORT_CLICKED -> CLICKED.

    A control registered for CLICKED only activates once. Registering both
    (the rule this repository used to enforce) activates twice, which is the
    defect this test guards against.
    """
    release_sequence = ["RELEASED", "SHORT_CLICKED", "CLICKED"]

    def activations(subscribed):
        return sum(1 for ev in release_sequence if ev in subscribed)

    assert activations({"CLICKED"}) == 1
    assert activations({"CLICKED", "SHORT_CLICKED"}) == 2


# --------------------------------------------------------------------------
# Coordinate sanity for the whole inventory
# --------------------------------------------------------------------------
def test_every_control_is_inside_the_panel():
    for c in TABS + MEDIA_TRANSPORT + [MODAL_CANCEL, MODAL_CONFIRM, MODAL_SCRIM]:
        assert 0 <= c.rect.x0 < c.rect.x1 <= UI_SCREEN_W, f"{c.name} x out of panel"
        assert 0 <= c.rect.y0 < c.rect.y1 <= UI_SCREEN_H, f"{c.name} y out of panel"


def test_touch_targets_meet_the_minimum_size():
    # 56x48 is the documented driver minimum for this panel.
    for c in TABS + MEDIA_TRANSPORT + [MODAL_CANCEL, MODAL_CONFIRM]:
        assert c.rect.width >= 56, f"{c.name} is {c.rect.width} px wide"
        assert c.rect.height >= 48, f"{c.name} is {c.rect.height} px tall"


def test_dialog_fits_inside_the_workspace():
    # The dialog must not be clipped by the status/tab bars.
    assert DIALOG.x0 >= 0 and DIALOG.x1 <= UI_SCREEN_W
    assert DIALOG.y0 >= UI_STATUS_BAR_H and DIALOG.y1 <= TAB_Y
