#!/usr/bin/env python3
"""Contrast gate for the HUD design tokens (spec §12.10).

§12.10 requires the palette to be *verified*, not assumed:
    body text  >= 7:1     dim text >= 4.5:1     icons/graphics >= 3:1

It also checks the dark and light palettes against their own backgrounds, which
is where hand-picked colours usually fail, and verifies that the RGB565 constants
in firmware/include/dashcue_config.h really are the tokens they claim to be
(an 8-bit -> 5/6/5 conversion is easy to get subtly wrong, and a wrong accent
colour is invisible until it is on the panel).

Usage:  python tools/contrast_check.py [--json]
Exit:   0 = every required pair meets its threshold, 1 = at least one fails.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# The report contains § characters; a Windows console defaulting to a legacy
# code page would abort the script instead of printing them.
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        try:
            _stream.reconfigure(encoding="utf-8", errors="replace")
        except (ValueError, OSError):  # pragma: no cover - platform dependent
            pass

REPO_ROOT = Path(__file__).resolve().parent.parent
CONFIG_H = REPO_ROOT / "firmware" / "include" / "dashcue_config.h"

# spec §12.10 token table
DARK = {
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
LIGHT = {
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


def contrast_ratio(fg: str, bg: str) -> float:
    l1 = relative_luminance(hex_to_rgb(fg))
    l2 = relative_luminance(hex_to_rgb(bg))
    lighter, darker = max(l1, l2), min(l1, l2)
    return (lighter + 0.05) / (darker + 0.05)


def to_rgb565(hex_colour: str) -> int:
    r, g, b = hex_to_rgb(hex_colour)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


# Required pairs: (name, foreground token, background token, minimum ratio)
def required_pairs(palette: dict[str, str]) -> list[tuple[str, str, str, float]]:
    return [
        ("body text on bg", palette["text"], palette["bg"], 7.0),
        ("body text on surface", palette["text"], palette["surface"], 7.0),
        ("dim text on bg", palette["text-dim"], palette["bg"], 4.5),
        ("dim text on surface", palette["text-dim"], palette["surface"], 4.5),
        # Icons and state colours are graphics, so §12.10 allows 3:1 - but they
        # must still clear it against the surface they are drawn on.
        ("accent icon on surface", palette["accent"], palette["surface"], 3.0),
        ("ok icon on surface", palette["ok"], palette["surface"], 3.0),
        ("warn icon on surface", palette["warn"], palette["surface"], 3.0),
        ("danger icon on surface", palette["danger"], palette["surface"], 3.0),
    ]


def check_firmware_tokens() -> list[str]:
    """Verify the RGB565 constants in dashcue_config.h match the tokens."""
    problems: list[str] = []
    if not CONFIG_H.exists():
        return [f"PENDING {CONFIG_H.relative_to(REPO_ROOT)} does not exist"]

    text = CONFIG_H.read_text(encoding="utf-8")
    expected = {
        "DASHCUE_ACCENT_RGB565": DARK["accent"],
        "DASHCUE_BG_RGB565": DARK["bg"],
        "DASHCUE_TEXT_RGB565": DARK["text"],
        "DASHCUE_TEXT_DIM_RGB565": DARK["text-dim"],
        "DASHCUE_OK_RGB565": DARK["ok"],
        "DASHCUE_WARN_RGB565": DARK["warn"],
        "DASHCUE_DANGER_RGB565": DARK["danger"],
    }
    for macro, colour in expected.items():
        m = re.search(rf"#define\s+{macro}\s+(0x[0-9A-Fa-f]+)", text)
        if not m:
            problems.append(f"MISSING {macro} is not defined in dashcue_config.h")
            continue
        actual = int(m.group(1), 16)
        want = to_rgb565(colour)
        if actual != want:
            problems.append(
                f"WRONG   {macro}=0x{actual:04X} but {colour} converts to 0x{want:04X}"
            )
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description="Check HUD design-token contrast (spec §12.10)")
    parser.add_argument("--json", action="store_true", help="emit machine-readable output")
    args = parser.parse_args()

    failures: list[str] = []
    rows: list[dict[str, object]] = []

    for palette_name, palette in (("dark", DARK), ("light", LIGHT)):
        for label, fg, bg, threshold in required_pairs(palette):
            ratio = contrast_ratio(fg, bg)
            ok = ratio >= threshold
            rows.append(
                {
                    "palette": palette_name,
                    "pair": label,
                    "fg": fg,
                    "bg": bg,
                    "ratio": round(ratio, 2),
                    "min": threshold,
                    "pass": ok,
                }
            )
            if not ok:
                failures.append(
                    f"{palette_name}: {label} {fg} on {bg} = {ratio:.2f}:1 (needs {threshold}:1)"
                )

    token_problems = check_firmware_tokens()
    failures.extend(token_problems)

    if args.json:
        print(json.dumps({"rows": rows, "failures": failures}, indent=2))
    else:
        print("DashCue design-token contrast check (spec §12.10)")
        print("-" * 74)
        print(f"{'palette':8} {'pair':28} {'ratio':>8} {'min':>5}  result")
        for r in rows:
            print(
                f"{r['palette']:8} {r['pair']:28} {r['ratio']:>7.2f}  {r['min']:>4}  "
                f"{'PASS' if r['pass'] else 'FAIL'}"
            )
        print("-" * 74)
        if token_problems:
            for p in token_problems:
                print(p)
        print(f"pairs checked: {len(rows)}  failures: {len(failures)}")

    if failures:
        print("RESULT: FAIL")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
