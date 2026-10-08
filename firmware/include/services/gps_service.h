#pragma once
#include <cstdint>

namespace svc {

struct GpsFix {
  bool valid;            // recent valid position (within GPS_STALE_MS)
  bool receiving;        // NMEA bytes arriving recently → UART wiring is alive
  bool speedValid;
  bool courseValid;
  double lat;
  double lon;
  float speedKmph;
  float courseDeg;
  uint32_t sats;
  float hdop;
  uint32_t ageMs;        // ms since the last valid position sentence
  uint32_t charsProcessed;
};

void gpsBegin();

// Non-blocking: drains the GPS stream into TinyGPSPlus.
// Call every loop().
void gpsUpdate();

GpsFix gpsFix();

// Diagnostic raw NMEA echo to the console (GPSRAW command). Works on hardware
// and sim; useful for proving the physical wiring.
void gpsSetRawEcho(bool on);
bool gpsRawEcho();

}  // namespace svc
