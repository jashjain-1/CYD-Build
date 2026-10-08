#pragma once
// ============================================================================
//  Waypoint route — REAL target coordinates, not dummy telemetry.
//  Distances/bearings are always computed from the live GPS fix; these are
//  only the destinations to guide toward.
//
//  EDIT THE COORDINATES for your campus/neighbourhood in demo_route.cpp.
// ============================================================================

struct DemoWaypoint {
  double lat;
  double lon;
  const char* name;
};

extern const DemoWaypoint DEMO_ROUTE[];
extern const int DEMO_WAYPOINT_COUNT;
