#pragma once
// ============================================================================
//  NMEA 0183 sentence framing helpers.
//  Pure C++ (no Arduino deps) so they run under a host compiler in
//  firmware/test/nav_nmea_host_test.cpp.
//
//  Used ONLY by the simulation GPS stream (SIM_BUILD=1). Hardware builds parse
//  real sentences from the NEO-6M with TinyGPSPlus and never call this.
// ============================================================================
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace nmea {

// XOR checksum over the sentence body (between '$' and '*').
inline uint8_t checksum(const char* body) {
  if (!body) return 0;
  uint8_t cs = 0;
  while (*body != '\0') {
    cs ^= static_cast<uint8_t>(*body++);
  }
  return cs;
}

// Frame a body as "$<body>*<HH>\r\n". Returns bytes written, or 0 on error/overflow.
inline std::size_t frame(const char* body, char* out, std::size_t outLen) {
  if (!body || !out || outLen == 0) return 0;
  const int n = std::snprintf(out, outLen, "$%s*%02X\r\n", body,
                              static_cast<unsigned>(checksum(body)));
  if (n <= 0 || static_cast<std::size_t>(n) >= outLen) return 0;
  return static_cast<std::size_t>(n);
}

// Convert decimal degrees to NMEA ddmm.mmmm format (value only, always positive;
// the hemisphere letter is emitted by the caller).
inline double toNmeaDegrees(double deg) {
  const double a = deg < 0.0 ? -deg : deg;
  const double d = std::floor(a);
  return d * 100.0 + (a - d) * 60.0;
}

}  // namespace nmea
