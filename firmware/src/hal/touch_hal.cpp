#include <Arduino.h>

#include "hal/touch_hal.h"
#include "config_build.h"
#include "config_pins.h"

#if SIM_BUILD
// ============================================================================
//  SIMULATION build — no native XPT2046 driver, so taps are injected through
//  the SAME interface the hardware driver exposes: touchPollTap(). Sources:
//  the "TAP x y" console command, plus an auto-demo that taps the Home buttons
//  so the UI is demonstrable without typing.
// ============================================================================
namespace {

int s_injectX = -1;
int s_injectY = -1;
uint32_t s_lastManualMs = 0;
uint32_t s_lastAutoMs = 0;
uint8_t s_demoStep = 0;

// Coordinates match the Home screen buttons / back chevron in ui/screens.cpp.
const int DEMO_TAPS[][2] = {
    {160, 58},   // DASHBOARD button
    {20, 40},    // back chevron
    {160, 108},  // NAVIGATION button
    {20, 40},    // back chevron
    {160, 158},  // MEDIA button
    {20, 40},    // back chevron
    {160, 208},  // SYSTEM button
    {20, 40},    // back chevron
};
const uint8_t DEMO_TAP_COUNT = sizeof(DEMO_TAPS) / sizeof(DEMO_TAPS[0]);

}  // namespace

namespace hal {

void touchBegin() {
  Serial.println(F("[SIM] touch substitute active ('TAP x y' / auto-demo)"));
}

void simTouchInject(int x, int y) {
  s_injectX = x;
  s_injectY = y;
  s_lastManualMs = millis();
}

bool touchPollTap(int& x, int& y) {
  if (s_injectX >= 0) {
    x = s_injectX;
    y = s_injectY;
    s_injectX = s_injectY = -1;
    return true;
  }
#if SIM_AUTO_DEMO
  const uint32_t now = millis();
  if (now - s_lastManualMs > 6000 && now - s_lastAutoMs > 4000) {
    s_lastAutoMs = now;
    x = DEMO_TAPS[s_demoStep][0];
    y = DEMO_TAPS[s_demoStep][1];
    s_demoStep = (uint8_t)((s_demoStep + 1) % DEMO_TAP_COUNT);
    return true;
  }
#endif
  return false;
}

}  // namespace hal

#elif defined(HEAD_UNIT_TARGET_CYD)
// ============================================================================
//  CYD hardware build — the XPT2046 sits on dedicated pins (its MISO lands on
//  GPIO 39, an input-only pad), so the controller is driven with bit-bang SPI
//  instead of a hardware peripheral. Command 0x90 requests X position, 0xD0 Y
//  position; after the 8-bit command there is one busy/turnaround clock, then
//  a 12-bit MSB-first result.
//
//  A median-of-3 filter per axis (ADR-0006) suppresses resistive ADC spikes
//  before the press→release tap logic. Raw calibration bounds and axis
//  direction MUST be confirmed on the physical panel in TEST-PROCEDURE
//  Stage 3 (TOUCHTEST); adjust TS_* / TOUCH_OFFSET_* below after that bench.
// ============================================================================
namespace {

constexpr uint8_t CMD_X  = 0x90;
constexpr uint8_t CMD_Y  = 0xD0;
constexpr uint8_t CMD_Z1 = 0xB0;
constexpr uint8_t CMD_Z2 = 0xC0;

// Raw 12-bit calibration bounds (bench-tune via TOUCHTEST on the real panel).
constexpr int TS_MIN = 200;
constexpr int TS_MAX = 3800;

// Pressure gating: when untouched, Z1 reads near 0 and Z2 near full scale.
constexpr uint16_t TOUCH_Z1_MIN = 100;   // below: pen up
constexpr uint16_t TOUCH_Z2_MAX = 4000;  // above: pen up

// Calibration fine-tune in screen pixels.
constexpr int TOUCH_OFFSET_X = 0;
constexpr int TOUCH_OFFSET_Y = 0;

bool s_down = false;
int s_px = 0, s_py = 0;
uint32_t s_tDownMs = 0;
uint32_t s_lastPollMs = 0;

void bbBegin() {
  pinMode(PIN_TOUCH_CS, OUTPUT);
  digitalWrite(PIN_TOUCH_CS, HIGH);
  pinMode(PIN_TOUCH_CLK, OUTPUT);
  digitalWrite(PIN_TOUCH_CLK, LOW);
  pinMode(PIN_TOUCH_MOSI, OUTPUT);
  digitalWrite(PIN_TOUCH_MOSI, LOW);
  pinMode(PIN_TOUCH_MISO, INPUT);  // input-only pad on GPIO 39
}

void bbWriteByte(uint8_t v) {
  for (int8_t bit = 7; bit >= 0; --bit) {
    digitalWrite(PIN_TOUCH_MOSI, (v >> bit) & 0x01);
    digitalWrite(PIN_TOUCH_CLK, HIGH);
    digitalWrite(PIN_TOUCH_CLK, LOW);
  }
}

uint16_t bbReadBits(uint8_t n) {
  uint16_t v = 0;
  for (uint8_t i = 0; i < n; ++i) {
    digitalWrite(PIN_TOUCH_CLK, HIGH);
    v = (uint16_t)((v << 1) | (digitalRead(PIN_TOUCH_MISO) ? 1 : 0));
    digitalWrite(PIN_TOUCH_CLK, LOW);
  }
  return v;
}

uint16_t bbTransaction(uint8_t cmd) {
  digitalWrite(PIN_TOUCH_CS, LOW);
  bbWriteByte(cmd);
  bbReadBits(1);               // busy/turnaround clock after the command byte
  const uint16_t raw = bbReadBits(12);
  digitalWrite(PIN_TOUCH_CS, HIGH);
  return raw;
}

uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
  if (a > b) { const uint16_t t = a; a = b; b = t; }
  if (b > c) { const uint16_t t = b; b = c; c = t; }
  if (a > b) { const uint16_t t = a; a = b; b = t; }
  return b;
}

uint16_t readChannelMedian(uint8_t cmd) {
  const uint16_t a = bbTransaction(cmd);
  const uint16_t b = bbTransaction(cmd);
  const uint16_t c = bbTransaction(cmd);
  return median3(a, b, c);
}

bool penDown(uint16_t z1, uint16_t z2) {
  return z1 > TOUCH_Z1_MIN && z2 < TOUCH_Z2_MAX;
}

}  // namespace

namespace hal {

void touchBegin() {
  bbBegin();
}

void simTouchInject(int, int) {}  // no-op on hardware

bool touchPollTap(int& x, int& y) {
  const uint32_t now = millis();
  if (now - s_lastPollMs < 20) return false;  // bound bit-bang CPU cost
  s_lastPollMs = now;

  const uint16_t z1 = bbTransaction(CMD_Z1);
  const uint16_t z2 = bbTransaction(CMD_Z2);

  if (penDown(z1, z2)) {
    const uint16_t rx = readChannelMedian(CMD_X);
    const uint16_t ry = readChannelMedian(CMD_Y);
    s_px = map(rx, TS_MIN, TS_MAX, 0, 319);
    s_py = map(ry, TS_MIN, TS_MAX, 0, 239);
    if (!s_down) {
      s_down = true;
      s_tDownMs = now;
    }
  } else if (s_down) {
    s_down = false;
    if (now - s_tDownMs < 1000) {  // reject stuck / ghost presses
      x = constrain(s_px + TOUCH_OFFSET_X, 0, 319);
      y = constrain(s_py + TOUCH_OFFSET_Y, 0, 239);
      return true;
    }
  }
  return false;
}

}  // namespace hal

#else
// ============================================================================
//  DevKit hardware build — real XPT2046 over the shared VSPI bus (legacy
//  wiring). The IRQ pin is optional (this driver polls). Three independent
//  samples are taken per poll and reduced with a median-of-3 filter (ADR-0006).
// ============================================================================
#include <XPT2046_Touchscreen.h>

namespace {

XPT2046_Touchscreen s_touch(PIN_TOUCH_CS);  // polling: no floating IRQ input

// Raw 12-bit ADC calibration bounds for 2.8" ILI9341 + XPT2046 (landscape rotation 1)
const int TS_MIN_X = 200;
const int TS_MAX_X = 3800;
const int TS_MIN_Y = 240;
const int TS_MAX_Y = 3800;

// Calibration fine-tune in screen pixels. Use the TOUCHTEST console command,
// tap the four corners, then adjust these until (0,0)/(319,239) land right.
const int TOUCH_OFFSET_X = 0;
const int TOUCH_OFFSET_Y = 0;

bool s_down = false;
int s_px = 0;
int s_py = 0;
uint32_t s_tDownMs = 0;
uint32_t s_lastPollMs = 0;

uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
  if (a > b) { const uint16_t t = a; a = b; b = t; }
  if (b > c) { const uint16_t t = b; b = c; c = t; }
  if (a > b) { const uint16_t t = a; a = b; b = t; }
  return b;
}

}  // namespace

namespace hal {

void touchBegin() {
  s_touch.begin();
  s_touch.setRotation(1);  // match the landscape display rotation
}

void simTouchInject(int, int) {}  // no-op on hardware

bool touchPollTap(int& x, int& y) {
  const uint32_t now = millis();
  if (now - s_lastPollMs < 20) return false;  // limit SPI traffic
  s_lastPollMs = now;

  if (s_touch.touched()) {
    // getPoint() performs a fresh conversion per call; three calls give three
    // independent samples for the median filter.
    const TS_Point p1 = s_touch.getPoint();
    const TS_Point p2 = s_touch.getPoint();
    const TS_Point p3 = s_touch.getPoint();
    const uint16_t mx = median3(p1.x, p2.x, p3.x);
    const uint16_t my = median3(p1.y, p2.y, p3.y);
    s_px = map(mx, TS_MIN_X, TS_MAX_X, 0, 319);
    s_py = map(my, TS_MIN_Y, TS_MAX_Y, 0, 239);
    if (!s_down) {
      s_down = true;
      s_tDownMs = now;
    }
  } else if (s_down) {
    s_down = false;
    if (now - s_tDownMs < 1000) {  // reject stuck / ghost presses
      x = constrain(s_px + TOUCH_OFFSET_X, 0, 319);
      y = constrain(s_py + TOUCH_OFFSET_Y, 0, 239);
      return true;
    }
  }
  return false;
}

}  // namespace hal
#endif
