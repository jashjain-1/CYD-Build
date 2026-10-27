#!/usr/bin/env python3
"""Generate firmware/include/lv_conf.h from the pinned LVGL template.

Why generate instead of committing a hand-written config: LVGL's configuration
file must agree with the exact LVGL version that is pinned (GATE-08). A copy that
was hand-edited once and then quietly kept while LVGL moved would be a silent,
invisible drift - exactly the kind of thing the specification's rule 4 exists to
prevent. Regenerating from the template makes the delta explicit and reviewable.

The values below are the ones this product needs; everything else keeps the
template's default.

Usage:  python tools/gen_lv_conf.py
Exit:   0 = written and verified, 1 = template missing or a setting not found.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TEMPLATE_GLOB = "firmware/.pio/libdeps/*/lvgl/lv_conf_template.h"
OUTPUT = REPO_ROOT / "firmware" / "include" / "lv_conf.h"

# name -> value, applied in order. Every one of these must exist in the template;
# a missing setting means the template changed and this script is stale, which
# must fail loudly rather than silently produce a different config.
SETTINGS: list[tuple[str, str]] = [
    # Diagnostics on: LVGL's own assertion messages are how a UI bug is caught on
    # a device with no debugger attached. LV_LOG_PRINTF routes them to the UART,
    # which is the only channel a flashed board has.
    ("LV_USE_LOG", "1"),
    ("LV_LOG_PRINTF", "1"),
    ("LV_LOG_LEVEL", "LV_LOG_LEVEL_ERROR"),
    # An assert must be diagnosable, never a silent halt: LVGL's default handler is
    # `while(1)`, which leaves a dead panel with no log and no way to tell why.
    ("LV_ASSERT_USE_CUSTOM_INCLUDE", "1"),
    ("LV_ASSERT_CUSTOM_INCLUDE", '"dashcue_lvgl_assert.h"'),
    # §16 memory plan budgeted ~40 KB for a static LVGL pool. Measured on hardware:
    # the 7 screens plus the shared status/tab bars need more than the whole static
    # DRAM budget allowed once the draw buffers are reserved, and a 40 KB pool ran
    # out mid-build (LV_ASSERT_MALLOC in lv_obj_add_style -> boot loop). LVGL
    # therefore allocates from the C library (the ESP32 runtime heap, ~318 KB free
    # at boot, measured) instead of a static array. Allocation happens at UI build
    # time, never in a hot path. See D-09 in docs/DECISIONS.md.
    ("LV_USE_STDLIB_MALLOC", "LV_STDLIB_CLIB"),
    # Retained for the LV_STDLIB_BUILTIN case; unused when CLIB is selected.
    ("LV_MEM_SIZE", "(48 * 1024U)"),
    # §16: UI render >= 20 FPS. 30 ms refresh period leaves headroom.
    ("LV_DEF_REFR_PERIOD", "30"),
    # Panel is 320x240 at ~2.8"; this keeps LVGL's default sizes sane.
    ("LV_DPI_DEF", "130"),
    ("LV_USE_OS", "LV_OS_NONE"),  # the UI task is the only LVGL caller (§8.4)
    # §12.10 typography: 12/16/20/28 px plus the default 14 px.
    ("LV_FONT_MONTSERRAT_12", "1"),
    ("LV_FONT_MONTSERRAT_14", "1"),
    ("LV_FONT_MONTSERRAT_16", "1"),
    ("LV_FONT_MONTSERRAT_20", "1"),
    ("LV_FONT_MONTSERRAT_28", "1"),
    # §12.4: distance-to-turn digits are 48 px, so the 48 px face is needed too.
    ("LV_FONT_MONTSERRAT_48", "1"),
    ("LV_FONT_DEFAULT", "&lv_font_montserrat_14"),
    ("LV_USE_ASSERT_NULL", "1"),
    ("LV_USE_ASSERT_MALLOC", "1"),
    # Widgets this UI never instantiates. Each was checked with a zero-reference
    # grep across firmware/src before being switched off; leaving them on links
    # widget tables and glyph data into a 3 MB app partition for nothing.
    ("LV_USE_CALENDAR", "0"),
    ("LV_USE_CHART", "0"),
    ("LV_USE_ROLLER", "0"),
    ("LV_USE_SPAN", "0"),
    ("LV_USE_LED", "0"),
    ("LV_USE_MSGBOX", "0"),
    ("LV_USE_KEYBOARD", "0"),
    ("LV_USE_TEXTAREA", "0"),
    # spinbox is a textarea subclass and fails to link without it.
    ("LV_USE_SPINBOX", "0"),
    ("LV_USE_DROPDOWN", "0"),
    ("LV_USE_SLIDER", "0"),
    ("LV_USE_SWITCH", "0"),
    ("LV_USE_TABLE", "0"),
    ("LV_USE_SPINNER", "0"),
]

HEADER = """/* DashCue lv_conf.h - generated from the pinned LVGL template by
 * tools/gen_lv_conf.py. DO NOT HAND-EDIT: re-run the generator, so this file
 * cannot drift from the LVGL version recorded in docs/DEPENDENCIES.md.
 */
"""


def find_template() -> Path | None:
    matches = sorted(REPO_ROOT.glob(TEMPLATE_GLOB))
    if not matches:
        return None
    # Prefer the highest-sorting env directory for determinism when several exist.
    return matches[-1]


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate lv_conf.h from the pinned LVGL template")
    parser.add_argument("--check", action="store_true", help="fail if the output would change")
    args = parser.parse_args()

    template = find_template()
    if template is None:
        print(
            "LVGL template not found under firmware/.pio/libdeps/*/lvgl/.\n"
            "Run `pio run -e cyd_ili9341` once so PlatformIO installs the pinned\n"
            "LVGL, then re-run this script.",
            file=sys.stderr,
        )
        return 1

    text = template.read_text(encoding="utf-8", errors="replace")

    # The template is wrapped in `#if 0` so it can live inside the library.
    text, enabled = re.subn(r"^#if 0", "#if 1", text, count=1, flags=re.MULTILINE)
    if enabled != 1:
        print(f"could not find the disabling `#if 0` in {template}", file=sys.stderr)
        return 1

    missing: list[str] = []
    for name, value in SETTINGS:
        pattern = re.compile(rf"^(\s*)#define\s+{re.escape(name)}\s+.*$", re.MULTILINE)
        text, count = pattern.subn(
            lambda m, _name=name, _value=value: f"{m.group(1)}#define {_name} {_value}",
            text,
            count=1,
        )
        if count == 0:
            missing.append(name)

    if missing:
        # A template that no longer contains a setting we rely on has changed
        # incompatibly; refuse to emit a config that silently differs.
        print(f"template is missing required setting(s): {', '.join(missing)}", file=sys.stderr)
        print(f"review {template} and update SETTINGS in this script", file=sys.stderr)
        return 1

    output = HEADER + text

    if args.check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""
        if current != output:
            print(f"{OUTPUT.relative_to(REPO_ROOT)} is out of date; re-run without --check")
            return 1
        print(f"{OUTPUT.relative_to(REPO_ROOT)} is up to date")
        return 0

    OUTPUT.write_text(output, encoding="utf-8")
    print(f"wrote {OUTPUT.relative_to(REPO_ROOT)} from {template.relative_to(REPO_ROOT)}")
    for name, value in SETTINGS:
        print(f"  {name} = {value}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
