"""
Adversarial Accessibility, Contrast, Touch Target, and Semantics Verification Suite
Target: Android Companion App UI & HUD LVGL 9 Firmware
Challenger: challenger_a11y_contrast_1
"""

import math
import re
from pathlib import Path
import pytest
import urllib.parse

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
ANDROID_UI = REPO_ROOT / "android" / "app" / "src" / "main" / "java" / "com" / "dashcue" / "app" / "ui"
FIRMWARE_UI = REPO_ROOT / "firmware" / "src" / "ui"
TOOLS_DIR = REPO_ROOT / "tools"

DARK_TOKENS = {
    "bg": "#05070A",
    "surface": "#0E131A",
    "surface2": "#18202A",
    "text": "#F2F5F7",
    "text-dim": "#A9B4C2",
    "accent": "#67B7FF",
    "ok": "#34D399",
    "warn": "#FFB454",
    "danger": "#FF6B6B",
}

LIGHT_TOKENS = {
    "bg": "#F8FAFC",
    "surface": "#E2E8F0",
    "surface2": "#CBD5E1",
    "text": "#0B1220",
    "text-dim": "#334155",
    "accent": "#0369A1",
    "ok": "#047857",
    "warn": "#B45309",
    "danger": "#B91C1C",
}

def hex_to_rgb(value: str) -> tuple[int, int, int]:
    value = value.lstrip("#")
    return int(value[0:2], 16), int(value[2:4], 16), int(value[4:6], 16)

def relative_luminance(rgb: tuple[int, int, int]) -> float:
    def channel(c: int) -> float:
        s = c / 255.0
        return s / 12.92 if s <= 0.04045 else ((s + 0.055) / 1.055) ** 2.4

    r, g, b = (channel(c) for c in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b

def contrast_ratio(c1: tuple[int, int, int], c2: tuple[int, int, int]) -> float:
    l1 = relative_luminance(c1)
    l2 = relative_luminance(c2)
    lighter, darker = max(l1, l2), min(l1, l2)
    return (lighter + 0.05) / (darker + 0.05)

def blend(fg: tuple[int, int, int], bg: tuple[int, int, int], alpha: float) -> tuple[int, int, int]:
    return tuple(int(round(fg[i] * alpha + bg[i] * (1.0 - alpha))) for i in range(3))


# ---------------------------------------------------------------------------
# 1. CONTRAST AUDIT (WCAG 2.1 AA / AAA)
# ---------------------------------------------------------------------------
def test_all_16_token_pairs_pass_wcag():
    """Verify all 16 design token color pairs satisfy spec §12.10 and WCAG 2.1 AA."""
    pairs = [
        ("body text on bg", "text", "bg", 7.0),
        ("body text on surface", "text", "surface", 7.0),
        ("dim text on bg", "text-dim", "bg", 4.5),
        ("dim text on surface", "text-dim", "surface", 4.5),
        ("accent icon on surface", "accent", "surface", 3.0),
        ("ok icon on surface", "ok", "surface", 3.0),
        ("warn icon on surface", "warn", "surface", 3.0),
        ("danger icon on surface", "danger", "surface", 3.0),
    ]

    for palette_name, palette in (("dark", DARK_TOKENS), ("light", LIGHT_TOKENS)):
        for label, fg_key, bg_key, min_ratio in pairs:
            fg_rgb = hex_to_rgb(palette[fg_key])
            bg_rgb = hex_to_rgb(palette[bg_key])
            ratio = contrast_ratio(fg_rgb, bg_rgb)
            assert ratio >= min_ratio, (
                f"FAIL: {palette_name} {label} ({palette[fg_key]} on {palette[bg_key]}) "
                f"= {ratio:.2f}:1, expected >= {min_ratio}:1"
            )


def test_status_pill_danger_contrast_mathematical_proof():
    """
    Verify mathematically that StatusPill in Components.kt with #EF4444
    on surface meets >= 4.5:1 with the updated 0.08 alpha container.
    Also prove that prior higher alpha (0.15) failed WCAG 2.1 AA (< 4.5:1).
    """
    danger_rgb = hex_to_rgb("EF4444")
    # Android theme surface is #111317; firmware surface is #0E131A; background is #050608
    surfaces = {
        "Android Surface": hex_to_rgb("111317"),
        "Firmware Surface": hex_to_rgb("0E131A"),
        "Android Bg": hex_to_rgb("050608"),
    }

    for s_name, surface_rgb in surfaces.items():
        # With 0.08 alpha
        blended_008 = blend(danger_rgb, surface_rgb, 0.08)
        cr_008 = contrast_ratio(danger_rgb, blended_008)
        assert cr_008 >= 4.5, (
            f"Expected StatusPill danger on {s_name} with 0.08 alpha to meet >= 4.5:1, "
            f"got {cr_008:.3f}:1"
        )

        # Contrast with 0.15 alpha should fail WCAG AA normal text (< 4.5:1)
        blended_015 = blend(danger_rgb, surface_rgb, 0.15)
        cr_015 = contrast_ratio(danger_rgb, blended_015)
        if "Surface" in s_name:
            assert cr_015 < 4.5, (
                f"Expected 0.15 alpha on {s_name} to fail (< 4.5:1), but got {cr_015:.3f}:1"
            )


def test_status_pill_all_states_pass_wcag():
    """Verify that all state colors used in StatusPill pass >= 4.5:1 on 0.08 alpha container."""
    state_colors = {
        "Danger": hex_to_rgb("EF4444"),
        "Ok": hex_to_rgb("10B981"),
        "Warn": hex_to_rgb("F59E0B"),
        "Accent": hex_to_rgb("E58B42"),
        "AccentAlt": hex_to_rgb("38BDF8"),
    }
    surface_rgb = hex_to_rgb("111317")

    for name, rgb in state_colors.items():
        blended = blend(rgb, surface_rgb, 0.08)
        cr = contrast_ratio(rgb, blended)
        assert cr >= 4.5, f"State {name} on blended surface failed WCAG 2.1 AA with {cr:.3f}:1"


# ---------------------------------------------------------------------------
# 2. TOUCH TARGET SIZING (Mobile >= 44x44 dp, HUD >= 56x48 px)
# ---------------------------------------------------------------------------
def test_mobile_touch_targets():
    """Verify mobile interactive touch targets (transport buttons, sliders, chips) meet >= 44x44 dp."""
    components_code = (ANDROID_UI / "Components.kt").read_text(encoding="utf-8")
    dashboard_code = (ANDROID_UI / "DashboardScreen.kt").read_text(encoding="utf-8")
    settings_code = (ANDROID_UI / "SettingsScreen.kt").read_text(encoding="utf-8")

    # 1. TransportButton
    m = re.search(r"modifier = Modifier\.size\(if \(accent\)\s*(\d+)\.dp\s*else\s*(\d+)\.dp\)", components_code)
    assert m, "TransportButton size modifier not found in Components.kt"
    accent_sz, normal_sz = int(m.group(1)), int(m.group(2))
    assert accent_sz >= 44, f"TransportButton accent size {accent_sz}dp < 44dp"
    assert normal_sz >= 44, f"TransportButton normal size {normal_sz}dp < 44dp"

    # 2. Scrubber slider in DashboardScreen
    assert "heightIn(min = 44.dp)" in dashboard_code, "Scrubber slider in DashboardScreen must enforce min 44.dp height"

    # 3. Brightness slider in SettingsScreen
    assert "heightIn(min = 44.dp)" in settings_code, "Brightness slider in SettingsScreen must enforce min 44.dp height"


def test_hud_primary_touch_targets():
    """Verify HUD primary touch targets in LVGL C++ code meet >= 56x48 px (DEC-29)."""
    media_code = (FIRMWARE_UI / "ui_screen_media.cpp").read_text(encoding="utf-8")
    settings_code = (FIRMWARE_UI / "ui_screen_settings.cpp").read_text(encoding="utf-8")
    safemode_code = (FIRMWARE_UI / "ui_screen_safemode.cpp").read_text(encoding="utf-8")

    # 1. Media transport buttons: Prev (56x48), Play/Pause (58x48), Next (56x48)
    # Check widths and height 48
    assert "{138, 56, LV_SYMBOL_PREV" in media_code
    assert "{196, 58, LV_SYMBOL_PLAY" in media_code
    assert "{256, 56, LV_SYMBOL_NEXT" in media_code
    assert "lv_obj_set_size(btn, spec.w, 48);" in media_code

    # 2. Volume buttons: 56x44 with ext_click_area(8) -> 72x60 px (>= 56x48 px automotive bound)
    assert "lv_obj_set_size(btn_vd, 56, 44);" in media_code
    assert "lv_obj_set_ext_click_area(btn_vd, 8);" in media_code
    assert "lv_obj_set_size(btn_vu, 56, 44);" in media_code
    assert "lv_obj_set_ext_click_area(btn_vu, 8);" in media_code

    # 3. Settings primary brightness buttons: 56x48 px
    assert "make_btn(m_row_brightness, 56, 48, -116" in settings_code
    assert "make_btn(m_row_brightness, 56, 48, -4" in settings_code

    # 4. SafeMode buttons: 260x48 px
    assert "lv_obj_set_size(btn, 260, 48);" in safemode_code


# ---------------------------------------------------------------------------
# 3. FORM VALIDATION & FEEDBACK
# ---------------------------------------------------------------------------
def simulate_is_valid_endpoint_url(url: str) -> bool:
    """Exact replica of SettingsScreen.kt isValidEndpointUrl implementation."""
    if not url or not url.strip():
        return True
    trimmed = url.strip()
    if not (trimmed.lower().startswith("http://") or trimmed.lower().startswith("https://")):
        return False
    try:
        parsed = urllib.parse.urlparse(trimmed)
        return bool(parsed.netloc and parsed.hostname)
    except Exception:
        return False


def test_form_endpoint_url_validation_rejection():
    """Verify that empty/invalid endpoint URLs are properly classified."""
    # Blank is allowed for clearing/defaulting
    assert simulate_is_valid_endpoint_url("") is True
    assert simulate_is_valid_endpoint_url("   ") is True

    # Invalid URLs must be rejected (return False)
    invalid_cases = [
        "http://",
        "https://",
        "http://   ",
        "invalid",
        "ftp://example.com",
        "file:///path",
        "http:///no-host",
        "htp://typo.com",
    ]
    for case in invalid_cases:
        assert simulate_is_valid_endpoint_url(case) is False, f"URL {case!r} should be rejected as invalid"

    # Valid URLs must be accepted (return True)
    valid_cases = [
        "http://192.168.1.10:5000",
        "https://nominatim.openstreetmap.org",
        "http://localhost:8080/path",
        "https://api.osrm.internal",
    ]
    for case in valid_cases:
        assert simulate_is_valid_endpoint_url(case) is True, f"URL {case!r} should be accepted as valid"


def test_form_validation_ui_feedback_elements():
    """Verify SettingsScreen.kt contains inline error icons, helper text, and live region feedback."""
    settings_code = (ANDROID_UI / "SettingsScreen.kt").read_text(encoding="utf-8")

    # Inline error states
    assert "isError = !isOsrmValid" in settings_code
    assert "isError = !isNominatimValid" in settings_code

    # Trailing error icon
    assert "Icons.Filled.Error" in settings_code
    assert 'contentDescription = "Error in OSRM URL"' in settings_code
    assert 'contentDescription = "Error in Nominatim URL"' in settings_code

    # Supporting text
    assert 'Must start with http:// or https:// and include a valid host' in settings_code

    # Live region feedback
    assert "LiveRegionMode.Polite" in settings_code
    assert 'contentDescription = "Settings: $saveFeedback"' in settings_code


# ---------------------------------------------------------------------------
# 4. SCREEN READER / TALKBACK SEMANTICS
# ---------------------------------------------------------------------------
def test_navigation_screen_announces_human_readable_labels():
    """Verify NavigationScreen.kt announces human-readable maneuver labels rather than raw protobuf enum names."""
    nav_code = (ANDROID_UI / "NavigationScreen.kt").read_text(encoding="utf-8")
    comp_code = (ANDROID_UI / "Components.kt").read_text(encoding="utf-8")

    # In NavigationScreen.kt:
    # Icon(maneuverIcon(nav.next), contentDescription = maneuverLabel(nav.next), ...)
    assert "contentDescription = maneuverLabel(nav.next)" in nav_code, (
        "NavigationScreen.kt turn icon must use maneuverLabel(nav.next) for contentDescription"
    )

    # Verify maneuverLabel does not return raw protobuf enum names
    # e.g. Maneuver.MV_TURN_LEFT -> "Turn left"
    raw_enums = ["MV_TURN_LEFT", "MV_UTURN", "MV_ROUNDABOUT", "MV_MERGE", "MV_ARRIVE"]
    for raw in raw_enums:
        assert f'Maneuver.{raw} -> "{raw}"' not in comp_code, f"maneuverLabel must not return raw enum {raw}"

    assert 'Maneuver.MV_TURN_LEFT -> "Turn left"' in comp_code
    assert 'Maneuver.MV_UTURN -> "Make a U-turn"' in comp_code
    assert 'Maneuver.MV_ROUNDABOUT -> "Enter the roundabout"' in comp_code


def test_slider_semantics_audit():
    """
    Adversarial audit on sliders:
    - Scrubber slider has contentDescription and stateDescription
    - Brightness slider has contentDescription and stateDescription
    - Volume slider: detect missing semantics as an empirical finding!
    """
    dashboard_code = (ANDROID_UI / "DashboardScreen.kt").read_text(encoding="utf-8")
    settings_code = (ANDROID_UI / "SettingsScreen.kt").read_text(encoding="utf-8")

    # 1. Scrubber slider
    assert 'contentDescription = "Playback position scrubber"' in dashboard_code
    assert 'stateDescription = "${formatClock(position)} of ${formatClock(duration)}"' in dashboard_code

    # 2. Brightness slider
    assert 'contentDescription = "Screen brightness"' in settings_code
    assert 'stateDescription = "${settings.brightnessPct}%"' in settings_code

    # 3. Volume slider: Check if it has contentDescription or stateDescription
    # Locate the volume Slider in DashboardScreen.kt
    vol_slider_match = re.search(
        r'Text\("Volume"[^)]+\)\s*Spacer[^;]+\s*Slider\(([^)]+)\)',
        dashboard_code,
        re.DOTALL
    )
    assert vol_slider_match, "Volume slider not found in DashboardScreen.kt"
    vol_slider_body = vol_slider_match.group(1)
    has_vol_semantics = "contentDescription" in vol_slider_body or "stateDescription" in vol_slider_body
    
    # We record this empirical observation
    print(f"\n[AUDIT] Volume Slider has explicit semantics: {has_vol_semantics}")
