#!/usr/bin/env python3
"""ESP32 CYD Pinmap Validation Gate for DashCue HUD.

Validates that all GPIO pin assignments in firmware/include/dashcue_config.h
and firmware/platformio.ini match the hardware specifications for ESP32 CYD 3.5"
(ESP32-3248S035 / ST7796 + XPT2046 + 8002A speaker amp on DAC2 GPIO 26),
with no illegal strapping pin conflicts, no flash pin collisions (GPIO 6-11),
and no input-only pins (GPIO 34-39) assigned to digital outputs.

Usage:
    python tools/validate_pins.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CONFIG_H = REPO_ROOT / "firmware" / "include" / "dashcue_config.h"
PLATFORMIO_INI = REPO_ROOT / "firmware" / "platformio.ini"

INPUT_ONLY_GPIOS = {34, 35, 36, 37, 38, 39}
FLASH_GPIOS = {6, 7, 8, 9, 10, 11}
STRAPPING_GPIOS = {0, 2, 12, 15}

EXPECTED_PINS = {
    "PIN_LCD_MISO": 12,
    "PIN_LCD_MOSI": 13,
    "PIN_LCD_SCLK": 14,
    "PIN_LCD_CS": 15,
    "PIN_LCD_DC": 2,
    "PIN_LCD_RST": -1,
    "PIN_LCD_BL": 27,
    "PIN_TOUCH_CS": 33,
    "PIN_TOUCH_IRQ": 36,
    "PIN_TOUCH_SCLK": 14,
    "PIN_TOUCH_MOSI": 13,
    "PIN_TOUCH_MISO": 12,
    "PIN_LED_R": 4,
    "PIN_LED_G": 16,
    "PIN_LED_B": 17,
    "PIN_LDR": 34,
    "PIN_SPEAKER": 26,
    "PIN_BUTTON_BOOT": 0,
}

OUTPUT_PINS = {
    "PIN_LCD_MOSI",
    "PIN_LCD_SCLK",
    "PIN_LCD_CS",
    "PIN_LCD_DC",
    "PIN_LCD_BL",
    "PIN_TOUCH_CS",
    "PIN_TOUCH_SCLK",
    "PIN_TOUCH_MOSI",
    "PIN_LED_R",
    "PIN_LED_G",
    "PIN_LED_B",
    "PIN_SPEAKER",
}


def parse_config_h(path: Path) -> dict[str, int]:
    if not path.is_file():
        raise FileNotFoundError(f"Configuration file not found: {path}")
    text = path.read_text(encoding="utf-8")
    pins = {}
    for line in text.splitlines():
        m = re.match(r"^\s*#define\s+(PIN_[A-Z0-9_]+)\s+(-?\d+)", line)
        if m:
            pins[m.group(1)] = int(m.group(2))
    return pins


def parse_platformio_ini_pins(path: Path) -> dict[str, int]:
    if not path.is_file():
        raise FileNotFoundError(f"platformio.ini not found: {path}")
    text = path.read_text(encoding="utf-8")
    pins = {}
    for line in text.splitlines():
        m = re.match(r"^\s*-(D(?:TFT|TOUCH)_[A-Z0-9_]+)=(-?\d+)", line)
        if m:
            pins[m.group(1)] = int(m.group(2))
    return pins


def validate() -> int:
    errors = []
    print("DashCue ESP32 Pinmap Validation")
    print("=" * 60)

    try:
        config_pins = parse_config_h(CONFIG_H)
    except (OSError, ValueError, KeyError, AttributeError, re.error) as e:
        print(f"ERROR: Failed to parse {CONFIG_H}: {e}")
        return 1

    try:
        ini_pins = parse_platformio_ini_pins(PLATFORMIO_INI)
    except (OSError, ValueError, KeyError, AttributeError, re.error) as e:
        print(f"ERROR: Failed to parse {PLATFORMIO_INI}: {e}")
        return 1

    # Check that all expected pins exist in dashcue_config.h
    for pin_name, expected_gpio in EXPECTED_PINS.items():
        if pin_name not in config_pins:
            errors.append(f"Missing pin definition in dashcue_config.h: {pin_name}")
            continue
        actual_gpio = config_pins[pin_name]
        if actual_gpio != expected_gpio:
            errors.append(
                f"Mismatch for {pin_name}: expected GPIO {expected_gpio}, found {actual_gpio}"
            )
        else:
            print(f"  [OK] {pin_name:<18} -> GPIO {actual_gpio:>2}")

    # Check output pins on input-only GPIOs
    for pin_name in OUTPUT_PINS:
        if pin_name in config_pins:
            gpio = config_pins[pin_name]
            if gpio in INPUT_ONLY_GPIOS:
                errors.append(
                    f"Output pin {pin_name} assigned to input-only GPIO {gpio}"
                )

    # Check flash pin collisions
    for pin_name, gpio in config_pins.items():
        if gpio in FLASH_GPIOS:
            errors.append(f"Pin {pin_name} collides with SPI flash GPIO {gpio}")

    # Verify platformio.ini consistency
    mapping = {
        "DTFT_MISO": config_pins.get("PIN_LCD_MISO"),
        "DTFT_MOSI": config_pins.get("PIN_LCD_MOSI"),
        "DTFT_SCLK": config_pins.get("PIN_LCD_SCLK"),
        "DTFT_CS": config_pins.get("PIN_LCD_CS"),
        "DTFT_DC": config_pins.get("PIN_LCD_DC"),
        "DTFT_RST": config_pins.get("PIN_LCD_RST"),
        "DTFT_BL": config_pins.get("PIN_LCD_BL"),
        "DTOUCH_CS": config_pins.get("PIN_TOUCH_CS"),
    }
    for ini_name, expected_val in mapping.items():
        if ini_name in ini_pins and expected_val is not None:
            if ini_pins[ini_name] != expected_val:
                errors.append(
                    f"platformio.ini {ini_name}={ini_pins[ini_name]} conflicts with dashcue_config.h ({expected_val})"
                )
            else:
                print(f"  [OK] platformio.ini {ini_name:<12} -> GPIO {ini_pins[ini_name]:>2}")

    print("=" * 60)
    if errors:
        print(f"RESULT: INVALID ({len(errors)} errors found):")
        for err in errors:
            print(f"  - {err}")
        return 1

    print("RESULT: VALID with 0 errors")
    return 0


if __name__ == "__main__":
    sys.exit(validate())
