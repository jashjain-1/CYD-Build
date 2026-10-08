#include "demo_route.h"

// ============================================================================
//  EDIT THESE COORDINATES for your own campus / neighbourhood.
//  Default: central Bengaluru. These are only the guidance TARGETS —
//  distances, bearings and turn angles are always computed from the live GPS fix.
// ============================================================================
const DemoWaypoint DEMO_ROUTE[] = {
    {12.97180, 77.59420, "CAMPUS GATE"},
    {12.97300, 77.59600, "LIBRARY"},
    {12.97450, 77.59400, "AUDITORIUM"},
    {12.97250, 77.59200, "PARKING"},
};

const int DEMO_WAYPOINT_COUNT = 4;
