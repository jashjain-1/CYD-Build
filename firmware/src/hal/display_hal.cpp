#include "hal/display_hal.h"
#include "config_pins.h"

#if defined(HEAD_UNIT_TARGET_CYD)
// ============================================================================
//  CYD (ESP32-2432S028R): the panel sits on the HSPI bus. The ESP32 Arduino
//  core's SPI.begin(sck, miso, mosi, ss) remap must happen BEFORE the panel
//  begin(), otherwise the constructor drives the default VSPI pins (which are
//  the CYD's microSD bus). TFT_RST is tied to EN, so -1 disables it.
//  The backlight (GPIO 21, active HIGH) must be driven or the panel stays dark.
// ============================================================================
#include <SPI.h>

namespace {
SPIClass s_hspi(HSPI);
Adafruit_ILI9341 s_display(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
}  // namespace

namespace hal {

bool displayBegin() {
  s_hspi.begin(PIN_TFT_SCK, PIN_TFT_MISO, PIN_TFT_MOSI, PIN_TFT_CS);
  s_display.begin(TFT_SPI_CLOCK);
  s_display.setRotation(1);  // landscape 320x240
  s_display.setTextWrap(false);
  s_display.fillScreen(0x0000);

  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);
  return true;
}

Adafruit_ILI9341& gfx() { return s_display; }

}  // namespace hal

#elif defined(HEAD_UNIT_TARGET_DEVKIT)
// ============================================================================
//  Legacy DevKit build: ILI9341 on the default VSPI bus (SCK 18 / MISO 19 /
//  MOSI 23, exactly the pins Adafruit_ILI9341::begin() initializes).
// ============================================================================
namespace {
Adafruit_ILI9341 s_display(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
}  // namespace

namespace hal {

bool displayBegin() {
  // Deselect both devices before the first SPI transaction.
  pinMode(PIN_TFT_CS, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
  pinMode(PIN_TOUCH_CS, OUTPUT);
  digitalWrite(PIN_TOUCH_CS, HIGH);
  s_display.begin(TFT_SPI_CLOCK);
  s_display.setRotation(1);  // landscape 320x240
  s_display.setTextWrap(false);
  s_display.fillScreen(0x0000);
  return true;
}

Adafruit_ILI9341& gfx() { return s_display; }

}  // namespace hal

#else
#error "display_hal.cpp: no supported target selected (see config_pins.h)"
#endif
