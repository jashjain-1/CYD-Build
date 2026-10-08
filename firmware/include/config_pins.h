#pragma once
// ============================================================================
//  Pin map selection.
//
//  Two build targets exist; the PlatformIO env sets exactly one:
//    HEAD_UNIT_TARGET_CYD=1    → config_pins_cyd.h
//                                 (ESP32-2432S028R "CYD", the target unit)
//    HEAD_UNIT_TARGET_DEVKIT=1 → config_pins_devkit.h
//                                 (legacy DevKit + separate display)
//
//  This header is the single include point: source files never include the
//  per-board headers directly, and every board-specific symbol must come from
//  a config_pins_*.h file.
// ============================================================================
#if defined(HEAD_UNIT_TARGET_CYD)
#include "config_pins_cyd.h"
#elif defined(HEAD_UNIT_TARGET_DEVKIT)
#include "config_pins_devkit.h"
#else
#error "No target selected: define HEAD_UNIT_TARGET_CYD=1 or HEAD_UNIT_TARGET_DEVKIT=1 (see platformio.ini)"
#endif
