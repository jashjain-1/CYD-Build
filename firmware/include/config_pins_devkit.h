#pragma once
// ============================================================================
//  Pin map — ESP32-WROOM-32 DevKit + separate ILI9341/XPT2046 breakout
//  (the pre-CYD wiring from the original prototype). Retained as the legacy
//  esp32dev / esp32dev_audio environment. Verify against docs/WIRING.md and do
//  not move pins without re-auditing conflicts:
//  GPIO 5 is a strapping pin (must idle HIGH — TFT CS idles HIGH: OK)
//  GPIO 16/17 are UART2 defaults but used here for TFT DC/RESET, so UART2 for
//  the GPS is EXPLICITLY remapped to 32/33 below.
// ============================================================================

// ---- TFT (ILI9341) on the VSPI bus ----
#define PIN_TFT_SCK   18  // VSPI default (implied by Adafruit_ILI9341 default ctor)
#define PIN_TFT_MISO  19
#define PIN_TFT_MOSI  23
#define PIN_TFT_CS    5
#define PIN_TFT_DC    16
#define PIN_TFT_RST   17
#define TFT_SPI_CLOCK 20000000  // breadboard jumpers: conservative clock

// ---- Touch (XPT2046) — shares the VSPI bus with the display, own CS ----
#define PIN_TOUCH_CS   4
#define PIN_TOUCH_IRQ  27   // optional; firmware polls, IRQ not required

// ---- GPS (NEO-6M) on UART2, remapped ----
#define PIN_GPS_RX    32   // ESP32 RX  <- GPS TX
#define PIN_GPS_TX    33   // ESP32 TX  -> GPS RX (configuration only)
#define GPS_BAUD      9600

// ---- Audio (OPTIONAL PAM8403) — tone output on DAC1 pin via LEDC ----
#define PIN_AUDIO     25
