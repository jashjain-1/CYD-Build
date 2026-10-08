#pragma once
// ============================================================================
//  Pure navigation math. No Arduino dependencies — this header compiles on a
//  desktop compiler, which is how firmware/test/nav_nmea_host_test.cpp verifies it.
// ============================================================================
#include <cmath>

namespace navmath {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusM = 6371008.8;  // mean Earth radius (meters)

inline double toRad(double deg) { return deg * (kPi / 180.0); }
inline double toDeg(double rad) { return rad * (180.0 / kPi); }

// Great-circle distance (haversine), meters.
inline double distanceMeters(double lat1, double lon1, double lat2, double lon2) {
  const double dLat = toRad(lat2 - lat1);
  const double dLon = toRad(lon2 - lon1);
  double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(toRad(lat1)) * std::cos(toRad(lat2)) *
                   std::sin(dLon / 2) * std::sin(dLon / 2);
  a = std::fmax(0.0, std::fmin(1.0, a));  // roundoff near antipodal points
  return 2.0 * kEarthRadiusM * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

// Initial bearing from point 1 to point 2, degrees in [0, 360).
inline double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  const double p1 = toRad(lat1);
  const double p2 = toRad(lat2);
  const double dl = toRad(lon2 - lon1);
  const double y = std::sin(dl) * std::cos(p2);
  const double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
  double b = toDeg(std::atan2(y, x));
  if (b < 0.0) b += 360.0;
  if (b >= 360.0) b -= 360.0;
  return b;
}

// Smallest signed turn from current course to target bearing, in [-180, 180).
// Positive = turn right, negative = turn left.
inline double turnDeg(double courseDeg, double targetBearingDeg) {
  double t = std::fmod(targetBearingDeg - courseDeg + 180.0, 360.0);
  if (t < 0.0) t += 360.0;
  t -= 180.0;
  return t;
}

// Local east/north offset in meters from a reference point (flat-earth approx,
// accurate enough for a <2 km navigation window).
inline void offsetMeters(double refLat, double refLon, double lat, double lon,
                         double& east, double& north) {
  east = toRad(lon - refLon) * kEarthRadiusM * std::cos(toRad(refLat));
  north = toRad(lat - refLat) * kEarthRadiusM;
}

}  // namespace navmath
