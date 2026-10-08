# CYD-Build

Independent firmware for the **ESP32-2432S028R "Cheap Yellow Display"** head
unit: standalone GPS waypoint navigation, trip computer, phone-cue turn-by-turn
overlay, media transport controls, and a five-screen touch UI on the 2.8"
320×240 ILI9341 panel.

CYD-Build is a clean-room re-implementation for the same physical hardware.
It shares no code, no history and no build state with any other repository.

## Hardware

| Peripheral | Connection |
|------------|-----------|
| TFT ILI9341 320×240 | HSPI (SCK 14 / MISO 12 / MOSI 13 / CS 15 / DC 2), backlight GPIO 21, 40 MHz |
| Touch XPT2046 (resistive) | bit-bang SPI on CLK 25 / CS 33 / MOSI 32 / MISO 39 / IRQ 36 (polled) |
| GPS u-blox NEO-6M | UART2 RX 27 / TX 22 @ 9600 (JST-1.25 expansion port) |
| Audio (optional) | GPIO 26 onboard amp — **disabled by default** (ADR-0003) |

Full pin tables: [docs/WIRING.md](docs/WIRING.md). Parts list: [docs/BOM.md](docs/BOM.md).

## Build

```bash
cd firmware
pio run -e cyd          # default target: the CYD board
pio run -e esp32dev     # legacy DevKit wiring
pio run -e sim          # simulation flags on the CYD pin map
pio run -e esp32dev_audio  # DevKit + LEDC tone output enabled
pio run -e cyd -t upload && pio device monitor
```

Requires PlatformIO (`pip install platformio`) and the pins declared in
[firmware/platformio.ini](firmware/platformio.ini).

## Test (host — no hardware needed)

```bash
./run-tests.sh     # Linux/macOS
run-tests.cmd      # Windows
```

Four builds with `g++ -std=c++11`, each reporting its own PASS/FAIL:

1. `nav_nmea_host_test` — haversine/bearing/turn math + NMEA framing/checksums
2. `services_hw` — nav/media services, console line handling, phone cues (`SIM_BUILD=0`)
3. `services_sim` — same suite against the simulation build (`SIM_BUILD=1`)
4. `gps_status_host_test` — GPS reception/staleness policy boundaries

Test binaries land in `.test-build/` (gitignored).

## Serial console (115200 baud)

```
HELP                          command list
STATUS                        full diagnostics
HOME | DASH | NAV | MEDIA | SYS   switch screen
CUE LEFT|250 m|MG Road|12 min|40  external turn cue (expires in 10 s)
THEME                         day/night palette toggle
TRIPRESET | NAVRESET          reset trip odometer / guidance
GPSRAW                        raw NMEA echo (wiring proof)
TOUCHTEST                     print taps instead of acting on them
BT | TAP x y                  simulation-only helpers
```

## Layout

```
firmware/
  platformio.ini      build environments and pinned dependencies
  include/            config, pure math, HAL/service/UI interfaces
  src/                implementation (hal/, services/, ui/, main.cpp)
  test/               host test suites + Arduino.h stub
docs/                 wiring, BOM, test procedure, phone protocol, ADRs
run-tests.sh|.cmd     host test runner
```

Feature inventory used as the regression baseline:
[docs/FEATURE-MATRIX.md](docs/FEATURE-MATRIX.md).
Design decisions: [docs/DECISIONS.md](docs/DECISIONS.md).
