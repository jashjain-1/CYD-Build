#pragma once
#include <Arduino.h>

namespace ui {

// Screen ids (used by serial console: HOME / NAV / MEDIA / SYS / DASH).
enum : uint8_t {
  SCREEN_HOME   = 0,
  SCREEN_NAV    = 1,
  SCREEN_MEDIA  = 2,
  SCREEN_SYS    = 3,
  SCREEN_DASH   = 4,
};

void uiBegin();
void uiTick();                    // non-blocking; call every loop()
void uiOnTap(int x, int y);       // screen coordinates
void uiShowScreen(uint8_t id);    // programmatic switch (console / tests)
uint8_t uiCurrentScreen();

}  // namespace ui
