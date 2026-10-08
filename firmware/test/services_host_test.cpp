// ============================================================================
//  Host test for firmware services: nav_service, media_service, console_input,
//  phone_cue.
//
//  Build & run (from repository root):
//    Hardware target (SIM_BUILD=0):
//      g++ -std=c++11 -DSIM_BUILD=0 -Ifirmware/include -Ifirmware/test/host \
//          firmware/test/services_host_test.cpp firmware/src/services/nav_service.cpp \
//          firmware/src/services/media_service.cpp firmware/src/demo_route.cpp \
//          -o .test-build/services_hw
//
//    Simulation target (SIM_BUILD=1):
//      g++ -std=c++11 -DSIM_BUILD=1 -Ifirmware/include -Ifirmware/test/host \
//          firmware/test/services_host_test.cpp firmware/src/services/nav_service.cpp \
//          firmware/src/services/media_service.cpp firmware/src/demo_route.cpp \
//          -o .test-build/services_sim
// ============================================================================
#include "services/nav_service.h"
#include "services/media_service.h"
#include "config_build.h"
#include "console_input.h"
#include "phone_cue.h"
#include "demo_route.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

static uint32_t nowMs = 0;
uint32_t millis() { return nowMs; }
static unsigned tones = 0, stops = 0;
namespace hal {
void audioTone(uint16_t, uint8_t) { ++tones; }
void audioStop() { ++stops; }
}

svc::GpsFix fixAt(double lon) {
  svc::GpsFix f = {};
  f.valid = f.speedValid = f.courseValid = true;
  f.lat = 12.97; f.lon = lon; f.speedKmph = 36; f.courseDeg = 90;
  return f;
}

int main() {
  using namespace svc;
  ConsoleLine line;
  assert(line.push('\n') == ConsoleLine::Pending);
  for (unsigned i = 0; i < 159; ++i) assert(line.push('a') == ConsoleLine::Pending);
  assert(line.push('\n') == ConsoleLine::Ready && std::strlen(line.text()) == 159);
  for (unsigned i = 0; i < 160; ++i) assert(line.push('b') == ConsoleLine::Pending);
  assert(line.push('\n') == ConsoleLine::Overflow);
  for (const char c : "NAV") { if (c) line.push(c); }
  assert(line.push('\r') == ConsoleLine::Pending);
  assert(line.push('\n') == ConsoleLine::Ready && !std::strcmp(line.text(), "NAV"));
  phonecue::Cue cue = {};
  assert(!phonecue::parse(nullptr, cue));
  assert(!phonecue::parse("", cue));
  assert(!phonecue::parse("||||", cue));
  assert(!phonecue::parse("|250 m|MG Road|12 min|40", cue));
  assert(!phonecue::parse("LEFT|250 m|MG Road|12 min|", cue));
  assert(!phonecue::parse("LEFT|1234567890123456|MG Road|12 min|40", cue));
  assert(!phonecue::parse("LEFT|250 m|MG Road|1234567890123456|40", cue));
  assert(phonecue::parse("LEFT|250 m|MG Road|12 min|40", cue));
  assert(!std::strcmp(cue.street, "MG Road") && cue.speed == 40 && cue.icon == TurnIcon::TurnLeft);
  assert(phonecue::parse("STRAIGHT|100 m|Main St|5 min|30", cue) && cue.icon == TurnIcon::Straight);
  assert(phonecue::parse("RIGHT|300 m|2nd St|6 min|25", cue) && cue.icon == TurnIcon::TurnRight);
  assert(phonecue::parse("SLEFT|400 m|3rd St|7 min|35", cue) && cue.icon == TurnIcon::SlightLeft);
  assert(phonecue::parse("SRIGHT|500 m|4th St|8 min|45", cue) && cue.icon == TurnIcon::SlightRight);
  assert(phonecue::parse("UTURN|50 m|5th St|1 min|10", cue) && cue.icon == TurnIcon::UTurn);
  assert(phonecue::parse("ROUND|150 m|Circle|3 min|20", cue) && cue.icon == TurnIcon::Roundabout);
  assert(phonecue::parse("ARRIVE|0 m|Dest|0 min|0", cue) && cue.icon == TurnIcon::Arrive);
  assert(!phonecue::parse("LEFT|250 m|MG Road|12 min|nan", cue));
  assert(!phonecue::parse("LEFT|250 m|MG Road|12 min|301", cue));
  assert(!phonecue::parse("LEFT|250 m|MG Road|12 min|-1", cue));
  assert(!phonecue::parse("LEFT|250 m|MG Road|12 min|20junk", cue));
  assert(!phonecue::parse("WRONG|250 m|MG Road|12 min|20", cue));
  assert(!phonecue::parse("LEFT|250 m||12 min|20", cue));
  assert(!phonecue::parse("LEFT|250 m|MG Road|12 min|20|extra", cue));

  navBegin();
  navUpdate(fixAt(77.59));
  nowMs = 500; navUpdate(fixAt(77.59005));
  nowMs = 1000; navUpdate(fixAt(77.59010));
  assert(navState().driveTimeS == 1 && navState().tripDistanceKm > 0.010f);
  navResetTrip();
  assert(navState().tripDistanceKm == 0 && navState().maxSpeedKmph == 0 && navState().driveTimeS == 0);
  nowMs += 1000; navUpdate(fixAt(77.59));
  nowMs += 1000; navUpdate(fixAt(78.00)); // reject teleport, rebase
  assert(navState().tripDistanceKm == 0);
  nowMs += 1000; navUpdate(fixAt(78.0001));
  assert(navState().tripDistanceKm > 0);
  const float beforeLoss = navState().tripDistanceKm;
  nowMs += 1000; navUpdate(GpsFix{});
  assert(!navState().navigating && !navState().speedValid && !navState().headingValid);
  nowMs += 1000; navUpdate(fixAt(78.001));
  assert(navState().tripDistanceKm == beforeLoss);
  GpsFix stopped = fixAt(78.0011); stopped.speedKmph = 0;
  nowMs += 1000; navUpdate(stopped);
  assert(navState().tripDistanceKm == beforeLoss && navState().turnIcon == TurnIcon::None);

  assert(navSetPhoneCue(TurnIcon::TurnLeft, "250 m", "MG Road", "12 min", 40));
  nowMs += 1000; navUpdate(GpsFix{});
  assert(navState().navigating && navState().source == NavSource::Phone);
  assert(!std::strcmp(navState().streetName, "MG Road") && !navState().headingValid);
  assert(!navSetPhoneCue(TurnIcon::TurnLeft, "250 m", "MG Road", "12 min",
                         std::numeric_limits<float>::quiet_NaN()));
  assert(!navSetPhoneCue(TurnIcon::TurnLeft, "250 m", "MG Road", "12 min",
                         std::numeric_limits<float>::infinity()));
  assert(!navSetPhoneCue(TurnIcon::TurnLeft, "250 m", "MG Road", "12 min", -0.1f));
  assert(!navSetPhoneCue(TurnIcon::TurnLeft, "250 m", "MG Road", "12 min", 300.1f));
  assert(!navSetPhoneCue(TurnIcon::None, "250 m", "MG Road", "12 min", 40));
  assert(!navSetPhoneCue(static_cast<TurnIcon>(9), "250 m", "MG Road", "12 min", 40));
  assert(!navSetPhoneCue(TurnIcon::TurnLeft, nullptr, "MG Road", "12 min", 40));
  assert(!navSetPhoneCue(TurnIcon::TurnLeft, "250 m", nullptr, "12 min", 40));
  assert(!navSetPhoneCue(TurnIcon::TurnLeft, "250 m", "MG Road", nullptr, 40));
  assert(!navSetPhoneCue(TurnIcon::TurnLeft, "", "MG Road", "12 min", 40));
  assert(!navSetPhoneCue(TurnIcon::TurnLeft, "250 m", "", "12 min", 40));
  assert(!navSetPhoneCue(TurnIcon::TurnLeft, "250 m", "MG Road", "", 40));
  assert(!navSetPhoneCue(TurnIcon::TurnLeft, "250 m", "1234567890123456789012345678901234", "-", 40));
  nowMs += PHONE_CUE_STALE_MS; navUpdate(GpsFix{});
  assert(!navState().navigating && navState().streetName[0] == 0);
  assert(navSetPhoneCue(TurnIcon::TurnRight, "1 km", "High Road", "-", 40));
  nowMs += PHONE_CUE_STALE_MS; navUpdate(fixAt(77.59));
  assert(navState().source == NavSource::Gps && navState().etaStr[0] == 0);
  nowMs = 0xfffffff0u;
  navReset();
  assert(navState().waypointCount == DEMO_WAYPOINT_COUNT);
  assert(navSetPhoneCue(TurnIcon::Straight, "1 km", "Road", "-", 10));
  nowMs += 1000; navUpdate(GpsFix{});
  assert(navState().source == NavSource::Phone);
  nowMs += PHONE_CUE_STALE_MS; navUpdate(GpsFix{});
  assert(navState().source == NavSource::None);

  // Nav GPS Fix validation edge cases (bounds & rejection)
  GpsFix badFix = fixAt(77.59);
  badFix.lat = 91.0; navUpdate(badFix); assert(!navState().navigating);
  badFix.lat = -91.0; navUpdate(badFix); assert(!navState().navigating);
  badFix.lat = 12.97; badFix.lon = 181.0; navUpdate(badFix); assert(!navState().navigating);
  badFix.lon = -181.0; navUpdate(badFix); assert(!navState().navigating);
  badFix.lon = 77.59; badFix.courseDeg = 360.0; navUpdate(badFix); assert(!navState().headingValid);
  badFix.courseDeg = -1.0; navUpdate(badFix); assert(!navState().headingValid);
  badFix.courseDeg = 90.0; badFix.speedKmph = 350.0; navUpdate(badFix); assert(!navState().speedValid);

  // Waypoint progression & looping test across DEMO_ROUTE
  navReset();
  assert(navState().waypointIndex == 0);
  GpsFix atWp0 = fixAt(77.59420); atWp0.lat = 12.97180;
  navUpdate(atWp0);
  assert(navState().waypointIndex == 1);
  GpsFix atWp1 = fixAt(77.59600); atWp1.lat = 12.97300;
  navUpdate(atWp1);
  assert(navState().waypointIndex == 2);
  GpsFix atWp2 = fixAt(77.59400); atWp2.lat = 12.97450;
  navUpdate(atWp2);
  assert(navState().waypointIndex == 3);
  GpsFix atWp3 = fixAt(77.59200); atWp3.lat = 12.97250;
  navUpdate(atWp3);
  assert(navState().waypointIndex == 0); // looped back to 0

  mediaBegin();
  // Media volume clamp checks
  for (int v = 0; v < 20; ++v) mediaVolumeUp();
  assert(mediaInfo().volume == 10);
  for (int v = 0; v < 20; ++v) mediaVolumeDown();
  assert(mediaInfo().volume == 0);
  for (int v = 0; v < 3; ++v) mediaVolumeUp();
  assert(mediaInfo().volume == 3);

  tones = stops = 0;
  mediaNext(); mediaTick(); assert(tones == 1);
  nowMs += 90; mediaTick(); assert(tones == 2);
  nowMs += 129; mediaTick(); assert(stops == 0);
  nowMs += 1; mediaTick(); assert(stops == 1);
#if SIM_BUILD
  simMediaSetConnected(false);
  mediaToggle(); assert(mediaInfo().state == MediaState::Stopped);
#else
  assert(!mediaIsConnected() && mediaInfo().state == MediaState::Stopped);
#endif
  std::puts("PASS: console boundaries, external cues, GPS trip recovery, waypoint loop, expiry/wraparound, tone timing, volume clamp");
}
