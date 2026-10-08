#pragma once

namespace hal {

void touchBegin();

// Non-blocking. Returns true and fills screen coordinates when a complete tap
// (press → release) happened since the last call. Hardware: XPT2046 over SPI.
// SIM: injected taps / auto-demo through this same interface.
bool touchPollTap(int& x, int& y);

// SIM builds only: inject a tap (used by the "TAP x y" console command).
// No-op on hardware builds.
void simTouchInject(int x, int y);

}  // namespace hal
