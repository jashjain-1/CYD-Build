#include "ui/theme.h"

namespace theme {

// Default Day Theme (High contrast automotive cyan/green)
uint16_t BG       = 0x0000;  // near-black
uint16_t SURFACE  = 0x18E3;  // dark slate
uint16_t SURFACE2 = 0x2124;  // slightly lighter
uint16_t TEXT     = 0xFFFF;  // crisp white
uint16_t MUTED    = 0x9CF3;  // light grey
uint16_t ACCENT   = 0x07FF;  // cyan (navigation / active)
uint16_t GOOD     = 0x07E0;  // green (fix / playing)
uint16_t WARN     = 0xFD20;  // amber
uint16_t BAD      = 0xF800;  // red

static bool s_night = false;

bool isNightMode() { return s_night; }

void setNightMode(bool night) {
  s_night = night;
  if (s_night) {
    // Amber palette; the TFT backlight remains at its wired brightness.
    BG       = 0x0000;
    SURFACE  = 0x1080;  // deep charcoal
    SURFACE2 = 0x2100;  // dim amber tint
    TEXT     = 0xFD20;  // warm amber text
    MUTED    = 0xB380;  // dim amber
    ACCENT   = 0xFBE0;  // bright amber
    GOOD     = 0xFD20;  // amber
    WARN     = 0xFA00;  // deep orange
    BAD      = 0xF800;  // deep red
  } else {
    // Day Mode: Crisp high-contrast palette
    BG       = 0x0000;
    SURFACE  = 0x18E3;
    SURFACE2 = 0x2124;
    TEXT     = 0xFFFF;
    MUTED    = 0x9CF3;
    ACCENT   = 0x07FF;
    GOOD     = 0x07E0;
    WARN     = 0xFD20;
    BAD      = 0xF800;
  }
}

void toggleNightMode() {
  setNightMode(!s_night);
}

}  // namespace theme
