#pragma once
#include <Arduino.h>

namespace hal {

// Hardware: Serial2 at 9600 on the remapped pins from config_pins.h.
// SIM: a checksummed NMEA generator (same Stream interface).
void gpsBegin();

// The NMEA byte source consumed by the GPS service (TinyGPSPlus).
Stream& gpsStream();

}  // namespace hal
