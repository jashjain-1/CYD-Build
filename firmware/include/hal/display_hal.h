#pragma once
#include <Adafruit_ILI9341.h>

namespace hal {

// Initializes SPI + panel, landscape 320x240. Returns false only on impossible config.
bool displayBegin();

// The live panel object for drawing (Adafruit_GFX API).
Adafruit_ILI9341& gfx();

}  // namespace hal
