#include "services/nav_service.h"
#include "config_build.h"
#include "demo_route.h"
#include "nav_math.h"
#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
int s_wpIndex = 0;
svc::NavState s_navState = {};
bool s_hasLastPos = false;
double s_lastLat = 0, s_lastLon = 0, s_distanceM = 0;
uint64_t s_driveMs = 0;
uint32_t s_lastUpdateMs = 0, s_phoneCueMs = 0;
bool s_phoneActive = false;

void setTurn(svc::TurnIcon icon) {
  using svc::TurnIcon;
  const char* label = "BEARING ONLY";
  switch (icon) {
    case TurnIcon::Straight: label = "KEEP STRAIGHT"; break;
    case TurnIcon::TurnLeft: label = "TURN LEFT"; break;
    case TurnIcon::TurnRight: label = "TURN RIGHT"; break;
    case TurnIcon::SlightLeft: label = "SLIGHT LEFT"; break;
    case TurnIcon::SlightRight: label = "SLIGHT RIGHT"; break;
    case TurnIcon::UTurn: label = "MAKE U-TURN"; break;
    case TurnIcon::Roundabout: label = "ROUNDABOUT"; break;
    case TurnIcon::Arrive: label = "ARRIVED"; break;
    case TurnIcon::None: break;
  }
  s_navState.turnIcon = icon;
  std::snprintf(s_navState.turnText, sizeof(s_navState.turnText), "%s", label);
}

bool fieldFits(const char* text, size_t capacity) {
  if (!text || !*text) return false;
  for (size_t i = 0; i < capacity; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == 0) return true;
    if (c < 32 || c > 126) return false;
  }
  return false;
}
}  // namespace

namespace svc {
void navResetTrip() {
  s_distanceM = 0;
  s_driveMs = 0;
  s_hasLastPos = false;
  s_lastUpdateMs = millis();
  s_navState.tripDistanceKm = 0;
  s_navState.maxSpeedKmph = 0;
  s_navState.avgSpeedKmph = 0;
  s_navState.driveTimeS = 0;
}

void navBegin() { navReset(); }

void navUpdate(const GpsFix& fix) {
  const uint32_t now = millis();
  const uint32_t dt = now - s_lastUpdateMs;
  s_lastUpdateMs = now;
  const bool valid = fix.valid && std::isfinite(fix.lat) && std::isfinite(fix.lon) &&
                     std::fabs(fix.lat) <= 90 && std::fabs(fix.lon) <= 180;
  const bool speedValid = valid && fix.speedValid && std::isfinite(fix.speedKmph) &&
                          fix.speedKmph >= 0 && fix.speedKmph <= 300;
  const bool courseValid = valid && fix.courseValid && std::isfinite(fix.courseDeg) &&
                           fix.courseDeg >= 0 && fix.courseDeg < 360 &&
                           speedValid && fix.speedKmph > 2;

  // GPS-only totals. Never bridge a loss of fix, long stall, or teleport.
  if (valid) {
    if (s_hasLastPos && dt > 0 && dt < GPS_STALE_MS && speedValid && fix.speedKmph > 2) {
      const double distance = navmath::distanceMeters(s_lastLat, s_lastLon, fix.lat, fix.lon);
      const double maxDistance = (300.0 / 3.6) * dt / 1000.0 + 10.0;
      if (distance <= maxDistance) {
        if (distance >= 1.0) s_distanceM += distance;
        s_driveMs += dt;  // preserve fractional seconds
      }
    }
    // Rebase even after rejecting a jump, so the odometer can recover.
    s_lastLat = fix.lat;
    s_lastLon = fix.lon;
    s_hasLastPos = true;
    if (speedValid && fix.speedKmph > s_navState.maxSpeedKmph)
      s_navState.maxSpeedKmph = fix.speedKmph;
  } else {
    s_hasLastPos = false;
  }
  s_navState.tripDistanceKm = static_cast<float>(s_distanceM / 1000.0);
  s_navState.driveTimeS = static_cast<uint32_t>(s_driveMs / 1000);
  s_navState.avgSpeedKmph = s_driveMs ? static_cast<float>(s_distanceM * 3600.0 / s_driveMs) : 0;
  s_navState.waypointCount = DEMO_WAYPOINT_COUNT;
  s_navState.waypointIndex = s_wpIndex;

  // External cues own guidance until expiry; GPS trip accounting continues.
  if (s_phoneActive && now - s_phoneCueMs < PHONE_CUE_STALE_MS) return;
  s_phoneActive = false;
  s_navState.source = valid ? NavSource::Gps : NavSource::None;
  s_navState.navigating = valid && DEMO_WAYPOINT_COUNT > 0;
  s_navState.speedValid = speedValid;
  s_navState.headingValid = courseValid;
  s_navState.currentSpeedKmph = speedValid ? fix.speedKmph : 0;
  s_navState.currentHeadingDeg = courseValid ? fix.courseDeg : 0;
  s_navState.etaStr[0] = '\0';  // a bearing cannot predict a road ETA
  if (!s_navState.navigating) {
    setTurn(TurnIcon::None);
    s_navState.distanceStr[0] = s_navState.streetName[0] = '\0';
    s_navState.distanceToNextM = s_navState.bearingToNextDeg = s_navState.turnDeg = 0;
    return;
  }

  double distance = 0;
  for (int guard = 0; guard <= DEMO_WAYPOINT_COUNT; ++guard) {
    distance = navmath::distanceMeters(fix.lat, fix.lon, DEMO_ROUTE[s_wpIndex].lat,
                                       DEMO_ROUTE[s_wpIndex].lon);
    if (distance >= NAV_ARRIVE_RADIUS_M || guard == DEMO_WAYPOINT_COUNT) break;
    s_wpIndex = (s_wpIndex + 1) % DEMO_WAYPOINT_COUNT;
  }
  const double bearing = navmath::bearingDeg(fix.lat, fix.lon, DEMO_ROUTE[s_wpIndex].lat,
                                             DEMO_ROUTE[s_wpIndex].lon);
  s_navState.waypointIndex = s_wpIndex;
  s_navState.distanceToNextM = static_cast<float>(distance);
  s_navState.bearingToNextDeg = static_cast<float>(bearing);
  s_navState.turnDeg = courseValid ? static_cast<float>(navmath::turnDeg(fix.courseDeg, bearing)) : 0;
  const float turn = s_navState.turnDeg;
  setTurn(!courseValid ? TurnIcon::None : std::fabs(turn) >= 150 ? TurnIcon::UTurn :
          turn > 45 ? TurnIcon::TurnRight : turn > 15 ? TurnIcon::SlightRight :
          turn < -45 ? TurnIcon::TurnLeft : turn < -15 ? TurnIcon::SlightLeft : TurnIcon::Straight);
  if (distance >= 1000)
    std::snprintf(s_navState.distanceStr, sizeof(s_navState.distanceStr), "%.1f km", distance / 1000);
  else
    std::snprintf(s_navState.distanceStr, sizeof(s_navState.distanceStr), "%.0f m", distance);
  std::snprintf(s_navState.streetName, sizeof(s_navState.streetName), "%s", DEMO_ROUTE[s_wpIndex].name);
}

bool navSetPhoneCue(TurnIcon icon, const char* dist, const char* street, const char* eta, float speed) {
  if (icon >= TurnIcon::None || !std::isfinite(speed) || speed < 0 || speed > 300 ||
      !fieldFits(dist, sizeof(s_navState.distanceStr)) ||
      !fieldFits(street, sizeof(s_navState.streetName)) ||
      !fieldFits(eta, sizeof(s_navState.etaStr))) return false;
  s_phoneCueMs = millis();
  s_phoneActive = true;
  s_navState.navigating = true;
  s_navState.source = NavSource::Phone;
  setTurn(icon);
  std::snprintf(s_navState.distanceStr, sizeof(s_navState.distanceStr), "%s", dist);
  std::snprintf(s_navState.streetName, sizeof(s_navState.streetName), "%s", street);
  std::snprintf(s_navState.etaStr, sizeof(s_navState.etaStr), "%s", eta);
  s_navState.currentSpeedKmph = speed;
  s_navState.speedValid = true;
  s_navState.headingValid = false;  // protocol does not supply heading
  s_navState.currentHeadingDeg = 0;
  return true;
}

void navReset() {
  s_wpIndex = 0;
  s_phoneActive = false;
  s_navState = NavState{};
  s_navState.turnIcon = TurnIcon::None;
  s_navState.waypointCount = DEMO_WAYPOINT_COUNT;
  navResetTrip();
}

NavState navState() { return s_navState; }
}  // namespace svc
