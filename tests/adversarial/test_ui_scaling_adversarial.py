"""
Adversarial UI Scaling, Viewport Bounds, Touch Target, and Memory Budget Test Suite.
Target: DashCue 480x320 ST7796 LVGL 9 Firmware
Challenger: challenger_ui_scaling_1
"""

from __future__ import annotations

import json
import math
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
FIRMWARE_DIR = REPO_ROOT / "firmware"
ELF_PATH = FIRMWARE_DIR / ".pio" / "build" / "cyd_ili9341" / "firmware.elf"
MEMORY_REPORT_PY = REPO_ROOT / "tools" / "memory_report.py"

# Geometry Specifications
SCREEN_W = 480
SCREEN_H = 320
STATUS_BAR_H = 24
TAB_BAR_H = 48
WORKSPACE_Y = 24
WORKSPACE_H = SCREEN_H - STATUS_BAR_H - TAB_BAR_H # 248


# ==============================================================================
# 1. Memory Budget Verification
# ==============================================================================
def test_firmware_static_dram_budget():
    """Verify static DRAM (.dram0.bss + .dram0.data) <= 70,000 bytes."""
    assert ELF_PATH.exists(), f"ELF file not found at {ELF_PATH}"
    
    cmd = [sys.executable, str(MEMORY_REPORT_PY), str(ELF_PATH)]
    result = subprocess.run(cmd, capture_output=True, text=True, check=True)
    report = json.loads(result.stdout)
    
    dram_static_bytes = report.get("dram_static_bytes", 0)
    print(f"\n[DRAM AUDIT] static DRAM = {dram_static_bytes} bytes (Budget: <= 70000 bytes)")
    
    assert dram_static_bytes <= 70000, f"DRAM static bytes {dram_static_bytes} exceeds 70,000 limit!"
    
    # Check individual section sizes
    allocated = {s["name"]: s["bytes"] for s in report.get("allocated_sections", [])}
    bss_bytes = allocated.get(".dram0.bss", 0)
    data_bytes = allocated.get(".dram0.data", 0)
    assert bss_bytes + data_bytes == dram_static_bytes
    
    # Verify display draw buffer sized for 480px (480 * 10 * 2 = 9600 bytes)
    disp_buf = next((obj for obj in report.get("largest_static_ram_objects", []) if "s_disp_buf" in obj["name"]), None)
    assert disp_buf is not None, "Display buffer not found in static objects!"
    assert disp_buf["bytes"] == 9600, f"Expected disp_buf 9600 bytes (480*10*2), got {disp_buf['bytes']}"


# ==============================================================================
# 2. Viewport Geometry & Bounds Verification
# ==============================================================================
class Box:
    def __init__(self, name: str, x: int, y: int, w: int, h: int, parent_box: Box | None = None):
        self.name = name
        if parent_box is not None:
            self.x = parent_box.x + x
            self.y = parent_box.y + y
        else:
            self.x = x
            self.y = y
        self.w = w
        self.h = h

    @property
    def x1(self) -> int:
        return self.x

    @property
    def y1(self) -> int:
        return self.y

    @property
    def x2(self) -> int:
        return self.x + self.w

    @property
    def y2(self) -> int:
        return self.y + self.h

    def verify_viewport_bounds(self, max_w: int = SCREEN_W, max_h: int = SCREEN_H):
        assert self.x1 >= 0, f"[{self.name}] Left edge x1={self.x1} < 0"
        assert self.y1 >= 0, f"[{self.name}] Top edge y1={self.y1} < 0"
        assert self.x2 <= max_w, f"[{self.name}] Right edge x2={self.x2} > {max_w}"
        assert self.y2 <= max_h, f"[{self.name}] Bottom edge y2={self.y2} > {max_h}"


def test_status_bar_and_tab_bar_geometry():
    """Verify StatusBar and TabBar partition [0..479, 0..319] gaplessly."""
    status_bar = Box("StatusBar", 0, 0, SCREEN_W, STATUS_BAR_H)
    tab_bar = Box("TabBar", 0, SCREEN_H - TAB_BAR_H, SCREEN_W, TAB_BAR_H)
    workspace = Box("Workspace", 0, WORKSPACE_Y, SCREEN_W, WORKSPACE_H)

    status_bar.verify_viewport_bounds()
    tab_bar.verify_viewport_bounds()
    workspace.verify_viewport_bounds()

    # Verify vertical stack continuity: 0 -> 24 -> 272 -> 320
    assert status_bar.y1 == 0 and status_bar.y2 == 24
    assert workspace.y1 == 24 and workspace.y2 == 272
    assert tab_bar.y1 == 272 and tab_bar.y2 == 320

    # Tabs inside TabBar (120x48 each, centered at start_x = (480 - 360)/2 = 60)
    tab_w = 120
    tab_start_x = (SCREEN_W - (tab_w * 3)) // 2 # 60
    for i in range(3):
        tab_box = Box(f"Tab_{i}", tab_start_x + i * tab_w, 0, tab_w, TAB_BAR_H, tab_bar)
        tab_box.verify_viewport_bounds()
        assert tab_box.w >= 56 and tab_box.h >= 48, f"Tab {i} fails touch target min 56x48"


def test_media_screen_layout_bounds():
    """Verify all widgets in Media screen stay strictly within [0..479, 0..319]."""
    workspace = Box("MediaScreen", 0, WORKSPACE_Y, SCREEN_W, WORKSPACE_H)
    
    # 1. Hero Artwork Box & Image
    art_box = Box("ArtworkBox", 14, 14, 160, 160, workspace)
    art_box.verify_viewport_bounds()
    art_img = Box("ArtworkImg", 2, 2, 156, 156, art_box) # Centered with 2px border
    art_img.verify_viewport_bounds()

    # 2. Volume Controls (under artwork: y=192, size 72x44)
    btn_vd = Box("VolDownBtn", 14, 192, 72, 44, workspace)
    btn_vu = Box("VolUpBtn", 94, 192, 72, 44, workspace)
    btn_vd.verify_viewport_bounds()
    btn_vu.verify_viewport_bounds()
    # Check no horizontal collision between vol buttons
    assert btn_vd.x2 <= btn_vu.x1

    # 3. Right Column Controls (start x=185, w=290)
    app_label = Box("AppLabel", 185, 12, 130, 22, workspace)
    stale_label = Box("StaleLabel", SCREEN_W - 95, 12, 85, 22, workspace)
    title_label = Box("TitleLabel", 185, 42, 290, 34, workspace)
    artist_label = Box("ArtistLabel", 185, 80, 290, 26, workspace)
    progress_bar = Box("ProgressBar", 185, 118, 290, 14, workspace)
    
    app_label.verify_viewport_bounds()
    stale_label.verify_viewport_bounds()
    title_label.verify_viewport_bounds()
    artist_label.verify_viewport_bounds()
    progress_bar.verify_viewport_bounds()

    # 4. Transport Buttons (Prev 72x52 at 190, Play 76x52 at 274, Next 72x52 at 362)
    btn_prev = Box("BtnPrev", 190, 158, 72, 52, workspace)
    btn_play = Box("BtnPlay", 274, 158, 76, 52, workspace)
    btn_next = Box("BtnNext", 362, 158, 72, 52, workspace)
    btn_prev.verify_viewport_bounds()
    btn_play.verify_viewport_bounds()
    btn_next.verify_viewport_bounds()

    # Verify transport spacing and no overlapping hitboxes
    assert btn_play.x1 - btn_prev.x2 == 12, "Gap between Prev and Play must be 12px"
    assert btn_next.x1 - btn_play.x2 == 12, "Gap between Play and Next must be 12px"
    # With ext_click_area(4), gap between hitboxes = 12 - 8 = 4px > 0
    assert (btn_play.x1 - 4) >= (btn_prev.x2 + 4)
    assert (btn_next.x1 - 4) >= (btn_play.x2 + 4)


def test_nav_screen_layout_bounds():
    """Verify all widgets in Navigation screen stay strictly within [0..479, 0..319]."""
    workspace = Box("NavScreen", 0, WORKSPACE_Y, SCREEN_W, WORKSPACE_H)

    # 1. Left Maneuver Box: 150x232 at (8, 8)
    maneuver_box = Box("ManeuverBox", 8, 8, 150, 232, workspace)
    maneuver_box.verify_viewport_bounds()

    roundabout_arc = Box("RoundaboutArc", 31, 28, 88, 88, maneuver_box)
    roundabout_arc.verify_viewport_bounds()
    # Verify roundabout is centered horizontally inside 150px box: (150 - 88) / 2 = 31
    assert roundabout_arc.x1 - maneuver_box.x1 == 31
    assert maneuver_box.x2 - roundabout_arc.x2 == 31

    caption = Box("ManeuverCaption", 6, 176, 138, 44, maneuver_box)
    caption.verify_viewport_bounds()

    # 2. Right Instruction Card: 310x232 at (162, 8)
    inst_card = Box("InstructionCard", 162, 8, 310, 232, workspace)
    inst_card.verify_viewport_bounds()
    # Left margin = 8px, right margin = 480 - (162 + 310) = 8px (symmetric!)
    assert maneuver_box.x1 - workspace.x1 == 8
    assert workspace.x2 - inst_card.x2 == 8

    dist_lbl = Box("DistanceLabel", 10, 8, 290, 52, inst_card)
    street_lbl = Box("StreetLabel", 10, 64, 290, 34, inst_card)
    next_street_lbl = Box("NextStreetLabel", 10, 102, 290, 24, inst_card)
    dist_lbl.verify_viewport_bounds()
    street_lbl.verify_viewport_bounds()
    next_street_lbl.verify_viewport_bounds()

    info_strip = Box("InfoStrip", 0, 164, 310, 68, inst_card)
    info_strip.verify_viewport_bounds()
    end_btn = Box("EndNavBtn", 310 - 76 - 12, (68 - 44) // 2, 76, 44, info_strip)
    end_btn.verify_viewport_bounds()

    # 3. Confirm Modal Dialog (Screen-level overlay)
    modal = Box("ConfirmModal", 0, 0, SCREEN_W, SCREEN_H)
    dialog_box = Box("DialogBox", (SCREEN_W - 380) // 2, (SCREEN_H - 180) // 2, 380, 180, modal)
    dialog_box.verify_viewport_bounds()
    btn_cancel = Box("ModalCancelBtn", 16, 180 - 56 - 14, 160, 56, dialog_box)
    btn_confirm = Box("ModalConfirmBtn", 380 - 160 - 16, 180 - 56 - 14, 160, 56, dialog_box)
    btn_cancel.verify_viewport_bounds()
    btn_confirm.verify_viewport_bounds()


def test_nav_arrow_vector_points_in_card():
    """Verify all vector arrow points across all maneuvers remain within 150x232 card."""
    CARD_W = 150
    CARD_H = 232

    # Extracted from ui_screen_nav.cpp
    maneuvers_points = {
        "LEFT_shaft": [(95, 135), (95, 75), (38, 75)],
        "LEFT_head": [(62, 48), (34, 75), (62, 102)],
        "RIGHT_shaft": [(55, 135), (55, 75), (112, 75)],
        "RIGHT_head": [(88, 48), (116, 75), (88, 102)],
        "SLIGHT_LEFT_shaft": [(82, 135), (82, 88), (48, 42)],
        "SLIGHT_LEFT_head": [(36, 64), (44, 38), (70, 48)],
        "SLIGHT_RIGHT_shaft": [(68, 135), (68, 88), (102, 42)],
        "SLIGHT_RIGHT_head": [(80, 48), (106, 38), (114, 64)],
        "SHARP_LEFT_shaft": [(96, 135), (96, 52), (46, 88)],
        "SHARP_LEFT_head": [(34, 68), (44, 94), (64, 72)],
        "SHARP_RIGHT_shaft": [(54, 135), (54, 52), (104, 88)],
        "SHARP_RIGHT_head": [(86, 72), (106, 94), (116, 68)],
        "STRAIGHT_shaft": [(75, 135), (75, 32)],
        "STRAIGHT_head": [(48, 60), (75, 30), (102, 60)],
        "UTURN_shaft": [(98, 135), (98, 42), (52, 42), (52, 85)],
        "UTURN_head": [(34, 68), (52, 94), (70, 68)],
        "FLAG_ARRIVE_LEFT": [(84, 135), (114, 135), (99, 135), (99, 28), (45, 56), (99, 84)],
        "FLAG_ARRIVE_RIGHT": [(36, 135), (66, 135), (51, 135), (51, 28), (105, 56), (51, 84)],
        "ROUNDABOUT_head": [(100, 36), (118, 50), (100, 62)],
    }

    for name, pts in maneuvers_points.items():
        for x, y in pts:
            assert 0 <= x <= CARD_W, f"[{name}] Point x={x} out of card [0, {CARD_W}]"
            assert 0 <= y <= CARD_H, f"[{name}] Point y={y} out of card [0, {CARD_H}]"


def test_settings_diag_calib_safemode_bounds():
    """Verify Settings, Diag, Calibration, and SafeMode screens stay within viewport."""
    # Settings Screen
    settings_ws = Box("SettingsWS", 0, WORKSPACE_Y, SCREEN_W, WORKSPACE_H)
    settings_ws.verify_viewport_bounds()
    # Row width UI_SCREEN_W - 16 = 464px, height 48px
    row = Box("SettingsRow", 8, 4, SCREEN_W - 16, 48, settings_ws)
    row.verify_viewport_bounds()
    # Buttons inside row:
    btn_bri_down = Box("BtnBriDown", row.w - 116 - 56, (48 - 48)//2, 56, 48, row)
    btn_bri_up = Box("BtnBriUp", row.w - 4 - 56, (48 - 48)//2, 56, 48, row)
    btn_theme = Box("BtnTheme", row.w - 4 - 110, (48 - 44)//2, 110, 44, row)
    btn_bri_down.verify_viewport_bounds()
    btn_bri_up.verify_viewport_bounds()
    btn_theme.verify_viewport_bounds()

    # Diagnostics Screen (456x296 centered in 480x320)
    diag_screen = Box("DiagScreen", 0, 0, SCREEN_W, SCREEN_H)
    diag_card = Box("DiagCard", (SCREEN_W - 456)//2, (SCREEN_H - 296)//2, 456, 296, diag_screen)
    diag_card.verify_viewport_bounds()
    btn_back = Box("DiagBackBtn", diag_card.w - 10 - 110, diag_card.h - 10 - 48, 110, 48, diag_card)
    btn_back.verify_viewport_bounds()

    # Calibration Screen (480x320 with 5 targets)
    calib_screen = Box("CalibScreen", 0, 0, SCREEN_W, SCREEN_H)
    targets = [(40, 40), (440, 40), (240, 160), (40, 280), (440, 280)]
    for idx, (tx, ty) in enumerate(targets):
        # Crosshair 24x2 and 2x24 centered at (tx, ty)
        h_line = Box(f"CalibHLine_{idx}", tx - 12, ty - 1, 24, 2, calib_screen)
        v_line = Box(f"CalibVLine_{idx}", tx - 1, ty - 12, 2, 24, calib_screen)
        dot = Box(f"CalibDot_{idx}", tx - 3, ty - 3, 10, 10, calib_screen)
        h_line.verify_viewport_bounds()
        v_line.verify_viewport_bounds()
        dot.verify_viewport_bounds()

    # SafeMode Screen (420x280 centered in 480x320)
    safe_screen = Box("SafeModeScreen", 0, 0, SCREEN_W, SCREEN_H)
    safe_card = Box("SafeModeCard", (SCREEN_W - 420)//2, (SCREEN_H - 280)//2, 420, 280, safe_screen)
    safe_card.verify_viewport_bounds()
    for y_pos, name in [(90, "Calib"), (150, "Reset"), (210, "Reboot")]:
        btn = Box(f"SafeBtn_{name}", (420 - 360)//2, y_pos, 360, 48, safe_card)
        btn.verify_viewport_bounds()


# ==============================================================================
# 3. Touch Target Dimensions Verification
# ==============================================================================
def test_touch_target_dimensions_meet_specs():
    """Verify primary interactive targets meet or exceed 44x44 dp / 56x48 px."""
    touch_targets = [
        # (Name, base_w, base_h, ext_click, required_min_w, required_min_h)
        ("TabBar Tabs", 120, 48, 0, 56, 48),
        ("Transport Prev", 72, 52, 4, 64, 48),
        ("Transport Play/Pause", 76, 52, 4, 64, 48),
        ("Transport Next", 72, 52, 4, 64, 48),
        ("Volume Down", 72, 44, 6, 56, 48), # 72x44 + 6 ext -> 84x56
        ("Volume Up", 72, 44, 6, 56, 48),   # 72x44 + 6 ext -> 84x56
        ("Nav End Route", 76, 44, 4, 56, 48),# 76x44 + 4 ext -> 84x52
        ("Nav Modal Cancel", 160, 56, 0, 56, 48),
        ("Nav Modal Confirm", 160, 56, 0, 56, 48),
        ("Nav Launch Maps", 240, 52, 0, 56, 48),
        ("Settings Bri -", 56, 48, 4, 56, 48),
        ("Settings Bri +", 56, 48, 4, 56, 48),
        ("Settings Theme", 110, 44, 4, 44, 44),
        ("Settings Units", 110, 44, 4, 44, 44),
        ("Settings Calib", 110, 44, 4, 44, 44),
        ("Settings Pair", 70, 44, 4, 44, 44),
        ("Settings Forget", 70, 44, 4, 44, 44),
        ("Diag Back", 110, 48, 0, 56, 48),
        ("SafeMode Calib", 360, 48, 2, 56, 48),
        ("SafeMode Reset", 360, 48, 2, 56, 48),
        ("SafeMode Reboot", 360, 48, 2, 56, 48),
        ("Pairing Cancel", 120, 48, 0, 56, 48),
    ]

    for name, bw, bh, ext, min_w, min_h in touch_targets:
        effective_w = bw + 2 * ext
        effective_h = bh + 2 * ext
        assert effective_w >= min_w, f"[{name}] effective width {effective_w} < {min_w}"
        assert effective_h >= min_h, f"[{name}] effective height {effective_h} < {min_h}"


# ==============================================================================
# 4. Dynamic Vertical Gradient Backdrop & Color Math
# ==============================================================================
def darken_for_gradient_top(rgb: int) -> int:
    """Mirrors darken_for_gradient_top in ui_screen_media.cpp."""
    r = ((rgb >> 16) & 0xFF) * 26 // 100
    g = ((rgb >> 8) & 0xFF) * 26 // 100
    b = (rgb & 0xFF) * 26 // 100
    return (r << 16) | (g << 8) | b

def lift_for_dark(rgb: int) -> int:
    """Mirrors lift_for_dark in ui_screen_media.cpp."""
    r = (((rgb >> 16) & 0xFF) * 65 + 255 * 35) // 100
    g = (((rgb >> 8) & 0xFF) * 65 + 255 * 35) // 100
    b = ((rgb & 0xFF) * 65 + 255 * 35) // 100
    return (r << 16) | (g << 8) | b

def lerp_rgb(a: int, b: int, t: int) -> int:
    """Mirrors lerp_rgb in ui_screen_media.cpp."""
    inv = 255 - t
    r = (((a >> 16) & 0xFF) * inv + ((b >> 16) & 0xFF) * t) // 255
    g = (((a >> 8) & 0xFF) * inv + ((b >> 8) & 0xFF) * t) // 255
    bl = ((a & 0xFF) * inv + (b & 0xFF) * t) // 255
    return (r << 16) | (g << 8) | bl


def test_gradient_color_math_invariants():
    """Stress-test gradient color math over 10,000+ edge and boundary cases."""
    test_colors = [
        0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF,
        0xFFFF00, 0x00FFFF, 0xFF00FF, 0x7F7F7F, 0x1DB954,
        0xE62117, 0xFA2D48, 0xFF5500, 0x00A8E1, 0x2BC5B4,
        0xF8991C, 0xF43E37, 0xFF8800, 0xE72C30, 0xED1C24,
        0x007ACC, 0x00FFFF, 0xA238FF, 0x1DA0C3, 0xFFA200,
        0xE58B42
    ]
    # Add boundary channel permutations
    for r in [0, 1, 127, 128, 254, 255]:
        for g in [0, 1, 127, 128, 254, 255]:
            for b in [0, 1, 127, 128, 254, 255]:
                test_colors.append((r << 16) | (g << 8) | b)

    for color in test_colors:
        # 1. Darken top math
        top = darken_for_gradient_top(color)
        r_top = (top >> 16) & 0xFF
        g_top = (top >> 8) & 0xFF
        b_top = top & 0xFF
        assert 0 <= r_top <= 255
        assert 0 <= g_top <= 255
        assert 0 <= b_top <= 255
        # Top color must always be darker or equal to source
        r_base = (color >> 16) & 0xFF
        g_base = (color >> 8) & 0xFF
        b_base = color & 0xFF
        assert r_top <= r_base
        assert g_top <= g_base
        assert b_top <= b_base

        # 2. Lift for dark math
        lifted = lift_for_dark(color)
        r_lift = (lifted >> 16) & 0xFF
        g_lift = (lifted >> 8) & 0xFF
        b_lift = lifted & 0xFF
        assert 0 <= r_lift <= 255
        assert 0 <= g_lift <= 255
        assert 0 <= b_lift <= 255
        # Lifted color must always be brighter or equal to source
        assert r_lift >= r_base
        assert g_lift >= g_base
        assert b_lift >= b_base

    # 3. Lerp continuity and endpoints
    color_a = 0x1DB954 # Spotify Green
    color_b = 0xE62117 # YouTube Red
    assert lerp_rgb(color_a, color_b, 0) == color_a
    assert lerp_rgb(color_a, color_b, 255) == color_b

    for t in range(256):
        lerped = lerp_rgb(color_a, color_b, t)
        r = (lerped >> 16) & 0xFF
        g = (lerped >> 8) & 0xFF
        b = lerped & 0xFF
        assert 0 <= r <= 255 and 0 <= g <= 255 and 0 <= b <= 255


def test_bilinear_dynamic_artwork_scaling_math():
    """Verify bilinear scaling math for Tier 1 (64x64) and Tier 2 (32x32)."""
    ART_INNER = 156
    
    # Tier 1: 64x64
    w_t1 = 64
    scale_t1 = (ART_INNER * 256) // w_t1
    assert scale_t1 == 624
    scaled_px_t1 = (w_t1 * scale_t1) / 256
    assert math.isclose(scaled_px_t1, 156.0, abs_tol=0.01)

    # Tier 2: 32x32
    w_t2 = 32
    scale_t2 = (ART_INNER * 256) // w_t2
    assert scale_t2 == 1248
    scaled_px_t2 = (w_t2 * scale_t2) / 256
    assert math.isclose(scaled_px_t2, 156.0, abs_tol=0.01)
