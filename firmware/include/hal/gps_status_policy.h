#pragma once
#include <cstdint>

#include "config_build.h"  // GPS_RECEIVING_MS / GPS_STALE_MS (pure macros, host-safe)

namespace hal {
namespace gpsstatus {

// Pure representation of the GPS reception/validity policy, evaluated from a
// snapshot of the parser state. Extracted from services/gps_service.cpp so the
// host test suite can exercise it without Arduino/TinyGPSPlus (per ADR-0004:
// keep safety policy pure and unit-tested).
struct Snapshot {
  uint32_t charsProcessed;   // total NMEA bytes the parser has consumed
  uint32_t msSinceLastChar;  // time since the byte counter last advanced
  uint32_t locationAgeMs;    // age of the last valid position sentence
  bool locationValid;        // parser flags the last position sentence valid
  uint32_t speedAgeMs;
  bool speedValid;
  uint32_t courseAgeMs;
  bool courseValid;
};

struct Verdict {
  bool receiving;   // bytes are still arriving (wiring alive)
  bool valid;       // position exists and is recent enough to act on
  bool speedValid;
  bool courseValid;
  uint32_t ageMs;   // reported position age
};

inline Verdict evaluate(const Snapshot& s) {
  Verdict v;
  v.receiving = (s.charsProcessed > 0) && (s.msSinceLastChar < GPS_RECEIVING_MS);
  // isValid() stays true after the receiver is unplugged; age() tracks commits.
  // Never report a fix stronger than the byte evidence: zero bytes parsed can
  // carry no valid position (defensive invariant; matches TinyGPSPlus on
  // hardware, where isValid() is false before the first sentence).
  v.ageMs = s.locationAgeMs;
  v.valid = s.locationValid && s.charsProcessed > 0 && s.locationAgeMs < GPS_STALE_MS;
  v.speedValid = v.valid && s.speedValid && s.speedAgeMs < GPS_STALE_MS;
  v.courseValid = v.valid && s.courseValid && s.courseAgeMs < GPS_STALE_MS;
  return v;
}

}  // namespace gpsstatus
}  // namespace hal
