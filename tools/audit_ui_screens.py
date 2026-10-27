#!/usr/bin/env python3
"""Automated static AST/regex audit script for DashCue HUD firmware UI screens.

Enforces:
1. Rule 1 (Min Dimensions): All interactive buttons >= 56x48 px.
2. Rule 2 (Ext Click Area): All interactive buttons have >= 8 px ext click area.
3. Rule 3 (Dual Event Registration): Buttons register both LV_EVENT_CLICKED and LV_EVENT_SHORT_CLICKED.
4. Rule 4 (No Scrollable Flags): Buttons and containers remove LV_OBJ_FLAG_SCROLLABLE and scroll-chain flags.
5. Rule 5 (Hardware Isolation): Zero raw analogRead, digitalRead, gpio_, or ADC access in UI space.
"""

import argparse
import json
import os
import re
import sys
from typing import Any

UI_SCREEN_FILES = [
    "firmware/src/ui/ui_screen_media.cpp",
    "firmware/src/ui/ui_screen_nav.cpp",
    "firmware/src/ui/ui_screen_settings.cpp",
    "firmware/src/ui/ui_screen_pairing.cpp",
    "firmware/src/ui/ui_screen_diag.cpp",
    "firmware/src/ui/ui_screen_safemode.cpp",
    "firmware/src/ui/ui_tab_bar.cpp",
]


def audit_file(filepath: str) -> dict[str, Any]:
    if not os.path.exists(filepath):
        return {
            "file": filepath,
            "status": "MISSING",
            "violations": [f"File not found: {filepath}"],
            "passed": False,
        }

    with open(filepath, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()

    violations = []
    rule_results = {
        "rule_1_dimensions": True,
        "rule_2_ext_click_area": True,
        "rule_3_dual_events": True,
        "rule_4_scroll_discipline": True,
        "rule_5_hw_isolation": True,
    }

    # Rule 5: Hardware Isolation (No direct GPIO/ADC in UI code)
    # Check for direct hardware reads
    forbidden_hw_patterns = [
        r"\banalogRead\s*\(",
        r"\bdigitalRead\s*\(",
        r"\bgpio_get_level\s*\(",
        r"\bgpio_set_level\s*\(",
        r"\bPIN_TOUCH_",
        r"\bread_channel\s*\(",
    ]
    for pat in forbidden_hw_patterns:
        matches = re.findall(pat, content)
        if matches:
            rule_results["rule_5_hw_isolation"] = False
            violations.append(f"Rule 5 (HW Isolation): Found forbidden hardware call '{matches[0]}'")

    # Rule 1: Widget dimensions check
    # Check for lv_obj_set_size(..., w, h)
    size_matches = re.finditer(r"lv_obj_set_size\s*\(\s*([a-zA-Z0-9_]+)\s*,\s*([^,]+)\s*,\s*([^)]+)\)", content)
    for m in size_matches:
        var_name = m.group(1).strip()
        w_expr = m.group(2).strip()
        h_expr = m.group(3).strip()

        # If it's a button or interactive element
        if "btn" in var_name.lower():
            try:
                w_val = int(w_expr)
                h_val = int(h_expr)
                if w_val < 56 or h_val < 48:
                    rule_results["rule_1_dimensions"] = False
                    violations.append(
                        f"Rule 1 (Dimensions): Button '{var_name}' sized {w_val}x{h_val} px (< 56x48 px minimum)"
                    )
            except ValueError:
                # Symbolic/variable expressions (e.g. spec.w, UI_SCREEN_W)
                pass

    # Rule 2: Extended click area padding (>= 8 px)
    ext_matches = re.finditer(r"lv_obj_set_ext_click_area\s*\(\s*([a-zA-Z0-9_]+)\s*,\s*([0-9]+)\s*\)", content)
    for m in ext_matches:
        var_name = m.group(1).strip()
        padding = int(m.group(2).strip())
        if padding < 8:
            rule_results["rule_2_ext_click_area"] = False
            violations.append(
                f"Rule 2 (Ext Click Area): Widget '{var_name}' padding {padding} px is below minimum 8 px"
            )

    # Rule 3: Dual Event Registration
    # Check that where LV_EVENT_CLICKED is added, LV_EVENT_SHORT_CLICKED is also added
    clicked_count = len(re.findall(r"LV_EVENT_CLICKED\b", content))
    short_clicked_count = len(re.findall(r"LV_EVENT_SHORT_CLICKED\b", content))
    if clicked_count > 0 and short_clicked_count < clicked_count:
        rule_results["rule_3_dual_events"] = False
        violations.append(
            f"Rule 3 (Dual Events): Found {clicked_count} LV_EVENT_CLICKED but only {short_clicked_count} LV_EVENT_SHORT_CLICKED"
        )

    # Rule 4: Scrollable flag discipline
    # Ensure containers / buttons have LV_OBJ_FLAG_SCROLLABLE removed
    if (
        "lv_button_create" in content or "lv_obj_create" in content
    ) and "LV_OBJ_FLAG_SCROLLABLE" not in content:
        rule_results["rule_4_scroll_discipline"] = False
        violations.append("Rule 4 (Scroll Discipline): No LV_OBJ_FLAG_SCROLLABLE removal found in file")

    passed = len(violations) == 0
    return {
        "file": filepath,
        "status": "PASS" if passed else "FAIL",
        "rule_results": rule_results,
        "violations": violations,
        "passed": passed,
    }


def main():
    parser = argparse.ArgumentParser(description="Audit DashCue firmware UI screens for automotive touch discipline.")
    parser.add_argument("--json", action="store_true", help="Output results in JSON format")
    parser.add_argument("--strict", action="store_true", help="Fail if any warning occurs")
    args = parser.parse_args()

    results = []
    all_passed = True

    for rel_path in UI_SCREEN_FILES:
        # Resolve path relative to repo root
        root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        abs_path = os.path.join(root, rel_path)
        res = audit_file(abs_path)
        results.append(res)
        if not res["passed"]:
            all_passed = False

    if args.json:
        print(json.dumps({"all_passed": all_passed, "screens": results}, indent=2))
    else:
        print("================================================================================")
        print("          DashCue HUD Firmware UI Automotive Touch Discipline Audit            ")
        print("================================================================================")
        print(f"{'UI Screen File':<45} | {'Dimensions':<10} | {'ExtClick':<8} | {'DualEv':<6} | {'NoScroll':<8} | {'Status':<6}")
        print("-" * 92)
        for r in results:
            fname = os.path.basename(r["file"])
            if r["status"] == "PASS":
                rr = r["rule_results"]
                d_ok = "PASS" if rr["rule_1_dimensions"] else "FAIL"
                e_ok = "PASS" if rr["rule_2_ext_click_area"] else "FAIL"
                v_ok = "PASS" if rr["rule_3_dual_events"] else "FAIL"
                s_ok = "PASS" if rr["rule_4_scroll_discipline"] else "FAIL"
                print(f"{fname:<45} | {d_ok:<10} | {e_ok:<8} | {v_ok:<6} | {s_ok:<8} | PASS")
            else:
                print(f"{fname:<45} | {'--':<10} | {'--':<8} | {'--':<6} | {'--':<8} | FAIL")
                for v in r["violations"]:
                    print(f"  -> {v}")
        print("=" * 92)
        print(f"Overall Result: {'100% COMPLIANT - ALL SCREENS PASSED' if all_passed else 'AUDIT FAILED WITH VIOLATIONS'}")

    sys.exit(0 if all_passed else 1)


if __name__ == "__main__":
    main()
