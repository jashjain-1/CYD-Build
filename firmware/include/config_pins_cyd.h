#pragma once
// ============================================================================
//  Pin map — ESP32-2432S028R "Cheap Yellow Display" (CYD), Techtonics TECH2554
//  single micro-USB variant (ILI9341 + XPT2046). Cross-checked against the
//  vendor page and the community-documented classic CYD pinout. See docs/WIRING.md.
//
//  The touch controller sits on dedicated pins (NOT the display's HSPI bus);
//  its MISO lands on GPIO 39, an input-only pad, so the touch driver uses
//  software (bit-bang) SPI instead of a hardware controller.
//
//  Select this file by defining HEAD_UNIT_TARGET_CYD=1 (env:cyd).
// ============================================================================

// ---- TFT (ILI9341) on the HSPI bus ----
#define PIN_TFT_SCK   14
#define PIN_TFT_MISO  12  // strapping pin; panel wiring drives it at boot, do not repurpose
#define PIN_TFT_MOSI  13
#define PIN_TFT_CS    15  // strapping pin; idles HIGH via CS discipline (ok)
#define PIN_TFT_DC     2  // strapping pin; fixed by the CYD PCB
// TFT_RST is not routed: the panel reset is tied to the ESP32 EN line. Pass -1.
#define PIN_TFT_RST   -1
#define PIN_TFT_BL    21  // backlight, active HIGH; must be driven or the panel stays dark
#define TFT_SPI_CLOCK 40000000  // 40 MHz: soldered CYD traces (bench-verify; fall back to 20 MHz if unstable)

// ---- Touch (XPT2046) on dedicated bit-bang pins ----
#define PIN_TOUCH_CLK 25
#define PIN_TOUCH_CS  33
#define PIN_TOUCH_MOSI 32
#define PIN_TOUCH_MISO 39  // input-only pad
#define PIN_TOUCH_IRQ  36  // input-only; firmware polls, IRQ unused

// ---- GPS (NEO-6M) on UART2, routed to the JST-1.25 expansion port (CN1/P3) ----
#define PIN_GPS_RX    27   // ESP32 RX  <- GPS TX
#define PIN_GPS_TX    22   // ESP32 TX  -> GPS RX (configuration only)
#define GPS_BAUD      9600

// ---- Onboard peripherals (documented; not driven by this firmware yet) ----
// RGB LED: GPIO 4 (red), 16 (green), 17 (blue), all active LOW.
// LDR light sensor: GPIO 34 (input-only, ADC1).
// Speaker: GPIO 26 (onboard 8002A amplifier). Audio is disabled per ADR-0002/0003.
#ifndef PIN_AUDIO
#define PIN_AUDIO 26
#endif
