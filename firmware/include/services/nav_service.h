#pragma once
#include "services/gps_service.h"

namespace svc {

enum class TurnIcon : uint8_t {
  Straight = 0,
  TurnLeft,
  TurnRight,
  SlightLeft,
  SlightRight,
  UTurn,
  Roundabout,
  Arrive,
  None
};

enum class NavSource : uint8_t { None, Gps, Phone };

struct NavState {
  bool navigating;         // true while active guidance / phone navigation is connected
  NavSource source;
  bool speedValid;
  bool headingValid;
  TurnIcon turnIcon;
  char turnText[16];       // "LEFT", "RIGHT", "STRAIGHT", etc.
  char distanceStr[16];    // "250 m", "1.2 km"
  char streetName[32];     // "MG Road", "Outer Ring Rd"
  char etaStr[16];         // "18:45" or "12 min"

  // Trip computer metrics from real GPS positions (phone cues do not add distance).
  float tripDistanceKm;
  float currentSpeedKmph;
  float maxSpeedKmph;
  float avgSpeedKmph;
  uint32_t driveTimeS;
  float currentHeadingDeg;

  // Waypoint simulation fields
  int waypointIndex;
  int waypointCount;
  float distanceToNextM;
  float bearingToNextDeg;
  float turnDeg;           // signed: >0 turn right, <0 turn left
};

void navBegin();

// Call ~1 Hz with the latest fix. Advances the target when within
// NAV_ARRIVE_RADIUS_M (route loops for demo purposes).
void navUpdate(const GpsFix& fix);

// Optional external cue; no Bluetooth transport or companion app is bundled.
// Returns false without changing state on malformed / oversized input.
bool navSetPhoneCue(TurnIcon icon, const char* dist, const char* street, const char* eta, float speed);

void navReset();
void navResetTrip();
NavState navState();

}  // namespace svc
