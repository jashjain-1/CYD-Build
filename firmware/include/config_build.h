#pragma once
// ============================================================================
//  CYD-Build — build configuration
//
//  SIM_BUILD = 0  →  HARDWARE build (DEFAULT). Real drivers only:
//                    ILI9341 + XPT2046 over SPI, NEO-6M NMEA over UART2,
//                    optional LEDC tone output. No synthetic data is compiled in.
//
//  SIM_BUILD = 1  →  Desktop simulation build. Substitutes exist ONLY inside
//                    #if SIM_BUILD blocks and keep the same software interfaces
//                    as the real drivers.
//
//  PlatformIO sets this via -D (see platformio.ini).
// ============================================================================
#ifndef SIM_BUILD
#define SIM_BUILD 0
#endif

#if SIM_BUILD != 0 && SIM_BUILD != 1
#error "SIM_BUILD must be 0 (hardware) or 1 (simulation)"
#endif

#define FW_NAME    "CYD Head-Unit"
#define FW_VERSION "1.0.0"

#define CONSOLE_BAUD 115200

// A fix is considered stale (screens show NO FIX) after this without a valid position.
#define GPS_STALE_MS 5000

// NMEA byte-flow window: "receiving" while bytes arrived within this period.
// Derived from GPS_STALE_MS so the two windows can never drift apart (ADR-0004).
#define GPS_RECEIVING_MS (GPS_STALE_MS * 3 / 5)

// Optional externally supplied cues expire unless the sender refreshes them.
#define PHONE_CUE_STALE_MS 10000

// Waypoint arrival radius in meters.
#define NAV_ARRIVE_RADIUS_M 25.0f

// SIM only: auto-inject demo taps so the UI is demonstrable without typing.
#ifndef SIM_AUTO_DEMO
#define SIM_AUTO_DEMO 0
#endif

// Audio is optional. Leave the GPIO inactive until an attenuated input is wired.
#ifndef AUDIO_ENABLED
#define AUDIO_ENABLED 0
#endif
