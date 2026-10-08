#pragma once
// ============================================================================
//  UI design tokens — Day / Night adaptive automotive theme.
//  Geometry icons and bounded text; primary home/media buttons are >=44 px tall.
//  Palette selection does not dim the physical TFT backlight.
// ============================================================================
#include <Arduino.h>

namespace theme {

// Dynamic RGB565 palette
extern uint16_t BG;
extern uint16_t SURFACE;
extern uint16_t SURFACE2;
extern uint16_t TEXT;
extern uint16_t MUTED;
extern uint16_t ACCENT;
extern uint16_t GOOD;
extern uint16_t WARN;
extern uint16_t BAD;

constexpr int SCREEN_W = 320;  // landscape
constexpr int SCREEN_H = 240;
constexpr int STATUS_H = 22;

bool isNightMode();
void setNightMode(bool night);
void toggleNightMode();

}  // namespace theme
