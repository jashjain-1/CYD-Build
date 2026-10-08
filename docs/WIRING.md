# Wiring

## Target A — ESP32-2432S028R "CYD" (env:cyd, default)

Purchased unit: Techtonics TECH2554, single micro-USB variant.
Source of truth: `firmware/include/config_pins_cyd.h`.

### TFT (ILI9341, HSPI bus)

| Signal | GPIO | Notes |
|--------|------|-------|
| SCK | 14 | HSPI clock |
| MISO | 12 | strapping pin; panel wiring drives it at boot — do not repurpose |
| MOSI | 13 | |
| CS | 15 | strapping pin; idles HIGH (CS discipline) |
| DC | 2 | strapping pin; fixed by the PCB |
| RST | — | tied to ESP32 EN on the PCB; driver passes -1 |
| Backlight | 21 | active HIGH; driven at init or the panel stays dark |

SPI clock: 40 MHz (soldered traces; fall back to 20 MHz if the panel shows
artifacts — see docs/TEST-PROCEDURE.md Stage 1).

### Touch (XPT2046, dedicated pins — NOT the HSPI bus)

| Signal | GPIO | Notes |
|--------|------|-------|
| CLK | 25 | bit-bang |
| CS | 33 | bit-bang |
| MOSI | 32 | bit-bang |
| MISO | 39 | **input-only pad** — hardware SPI cannot be used |
| IRQ | 36 | input-only; unused (driver polls at ≥20 ms intervals) |

### GPS (NEO-6M on UART2 → JST-1.25 expansion port CN1/P3)

| Signal | GPIO | Notes |
|--------|------|-------|
| ESP32 RX | 27 | ← GPS TX |
| ESP32 TX | 22 | → GPS RX (configuration only) |
| Baud | — | 9600 8N1 |

### Onboard peripherals (documented, not driven)

- RGB LED: GPIO 4 (red) / 16 (green) / 17 (blue), active LOW
- LDR: GPIO 34 (input-only, ADC1)
- Speaker amp (8002A): GPIO 26 — audio disabled by default (ADR-0003)

## Target B — Legacy ESP32 DevKit (env:esp32dev / esp32dev_audio)

Source of truth: `firmware/include/config_pins_devkit.h`.

- TFT on VSPI: SCK 18 / MISO 19 / MOSI 23 / CS 5 / DC 16 / RST 17 @ 20 MHz
- Touch XPT2046 shares VSPI with its own CS 4 (hardware SPI, library driver)
- GPS UART2 remapped to RX 32 / TX 33 (GPIO 16/17 belong to the TFT here)
- Optional PAM8403 tone out on GPIO 25 (env:esp32dev_audio only)
