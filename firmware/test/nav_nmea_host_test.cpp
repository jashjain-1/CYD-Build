// ============================================================================
//  Host test for the PURE parts of the firmware: nav_math.h + nmea_builder.h.
//  These headers have no Arduino dependencies, so this runs on a desktop
//  compiler — real verification without hardware or the ESP32 toolchain.
//
//  Build & run (from repository root):
//    g++ -std=c++11 -Ifirmware/include firmware/test/nav_nmea_host_test.cpp \
//        -o .test-build/test_nav
// ============================================================================
#include <cmath>
#include <cstdio>
#include <cstring>

#include "nav_math.h"
#include "nmea_builder.h"

static int g_failures = 0;

#define CHECK(cond, name)                     \
  do {                                        \
    if (cond) {                               \
      std::printf("PASS  %s\n", name);        \
    } else {                                  \
      std::printf("FAIL  %s\n", name);        \
      ++g_failures;                           \
    }                                         \
  } while (0)

static bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

int main() {
  std::printf("== nav_math / nmea_builder host checks ==\n");

  // ---- checksum ----
  CHECK(nmea::checksum(nullptr) == 0x00, "checksum of null body is 0");
  CHECK(nmea::checksum("") == 0x00, "checksum of empty body is 0");
  // Hand-verified: XOR('G','P','G','G','A') = 0x47^0x50^0x47^0x47^0x41 = 0x56
  CHECK(nmea::checksum("GPGGA") == 0x56, "checksum('GPGGA') == 0x56");
  {
    char both[32];
    std::snprintf(both, sizeof(both), "%s%s", "GPRMC,123", "519.00,A");
    const uint8_t split = (uint8_t)(nmea::checksum("GPRMC,123") ^ nmea::checksum("519.00,A"));
    CHECK(nmea::checksum(both) == split, "checksum is associative across a split body");
  }

  // ---- framing ----
  char out[128];
  const char* body = "GPGGA,123519,4807.038,N";
  CHECK(nmea::frame(nullptr, out, sizeof(out)) == 0, "frame() rejects null body");
  CHECK(nmea::frame(body, nullptr, sizeof(out)) == 0, "frame() rejects null out buffer");
  CHECK(nmea::frame(body, out, 0) == 0, "frame() rejects zero outLen");
  const std::size_t n = nmea::frame(body, out, sizeof(out));
  CHECK(n > 0, "frame() returns a length");
  CHECK(out[0] == '$', "frame starts with '$'");
  CHECK(n >= 2 && out[n - 2] == '\r' && out[n - 1] == '\n', "frame ends with CRLF");
  const char* star = std::strchr(out, '*');
  CHECK(star != nullptr, "frame contains '*'");
  if (star != nullptr) {
    CHECK((std::size_t)(star - out) == 1 + std::strlen(body), "'*' sits directly after the body");
    char hex[3] = {star[1], star[2], '\0'};
    unsigned emitted = 0;
    std::sscanf(hex, "%x", &emitted);
    CHECK((uint8_t)emitted == nmea::checksum(body), "emitted checksum matches the body");
  }
  char tiny[8];
  CHECK(nmea::frame("GPGGA,123519", tiny, sizeof(tiny)) == 0,
        "frame() rejects an undersized buffer");
  char exactBoundary[16];
  char exactFail[15];
  CHECK(nmea::frame("GPGGA,123", exactFail, sizeof(exactFail)) == 0,
        "frame() rejects buffer missing null terminator byte");
  CHECK(nmea::frame("GPGGA,123", exactBoundary, sizeof(exactBoundary)) == 15,
        "frame() succeeds on exactly sized buffer");

  // ---- ddmm.mmmm conversion ----
  CHECK(near(nmea::toNmeaDegrees(0.0), 0.0, 1e-9), "toNmeaDegrees(0) == 0");
  CHECK(near(nmea::toNmeaDegrees(1.5), 130.0, 1e-9), "toNmeaDegrees(1.5) == 130.0");
  CHECK(near(nmea::toNmeaDegrees(-1.5), 130.0, 1e-9), "toNmeaDegrees(-1.5) == 130.0 (abs)");

  // ---- navigation math ----
  CHECK(navmath::distanceMeters(12.9718, 77.5942, 12.9718, 77.5942) == 0.0,
        "distance to self is 0");
  // One degree of latitude on a sphere = R * (pi/180) = 111194.93 m
  CHECK(near(navmath::distanceMeters(0.0, 0.0, 1.0, 0.0), 111194.93, 1.0),
        "one degree of latitude ~= 111194.93 m");
  // Antipodal points: half circumference
  CHECK(near(navmath::distanceMeters(0.0, 0.0, 0.0, 180.0), navmath::kEarthRadiusM * navmath::kPi, 1.0),
        "antipodal points distance == pi * R");
  CHECK(near(navmath::bearingDeg(0.0, 0.0, 1.0, 0.0), 0.0, 1e-6), "bearing north == 0 deg");
  CHECK(near(navmath::bearingDeg(0.0, 0.0, 0.0, 1.0), 90.0, 1e-6), "bearing east == 90 deg");
  CHECK(near(navmath::bearingDeg(0.0, 0.0, -1.0, 0.0), 180.0, 1e-6), "bearing south == 180 deg");
  CHECK(near(navmath::bearingDeg(0.0, 0.0, 0.0, -1.0), 270.0, 1e-6), "bearing west == 270 deg");
  CHECK(navmath::bearingDeg(0.0, 0.0, 0.0, 0.0) == 0.0, "bearing to self == 0 deg");
  const double bNorthBound = navmath::bearingDeg(0.0, 0.0, 0.0000001, -0.00000000001);
  CHECK(bNorthBound >= 0.0 && bNorthBound < 360.0, "bearing lies strictly in [0, 360)");
  CHECK(near(navmath::turnDeg(0.0, 0.0), 0.0, 1e-9), "turn 0->0 == 0");
  CHECK(near(navmath::turnDeg(360.0, 0.0), 0.0, 1e-9), "turn 360->0 == 0");
  CHECK(near(navmath::turnDeg(10.0, 95.0), 85.0, 1e-9), "turn 10->95 == +85 (right)");
  CHECK(near(navmath::turnDeg(350.0, 5.0), 15.0, 1e-9), "turn 350->5 == +15 (wrap north)");
  CHECK(near(navmath::turnDeg(5.0, 350.0), -15.0, 1e-9), "turn 5->350 == -15 (wrap south)");
  CHECK(near(navmath::turnDeg(180.0, 0.0), -180.0, 1e-9), "turn 180->0 == -180 (exact opposite)");
  CHECK(near(navmath::turnDeg(0.0, 180.0), -180.0, 1e-9), "turn 0->180 == -180 (exact opposite)");

  const double leg1 = navmath::distanceMeters(12.9718, 77.5942, 12.9730, 77.5960);
  const double leg2 = navmath::distanceMeters(12.9730, 77.5960, 12.9718, 77.5942);
  CHECK(near(leg1, leg2, 1e-6), "distance is symmetric");
  CHECK(leg1 > 200.0 && leg1 < 260.0, "demo route leg ~230 m in expected range");

  double east = 0.0, north = 0.0;
  navmath::offsetMeters(0.0, 0.0, 0.01, 0.01, east, north);
  CHECK(east > 0.0 && north > 0.0, "offsetMeters: NE is (+,+)");

  std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED",
              g_failures, g_failures == 1 ? "" : "s");
  return g_failures == 0 ? 0 : 1;
}
