// ============================================================================
//  Host test for the pure GPS reception/validity policy in
//  firmware/include/hal/gps_status_policy.h (no Arduino / TinyGPSPlus needed).
//
//  Build & run (from repository root):
//    g++ -std=c++11 -Ifirmware/include firmware/test/gps_status_host_test.cpp \
//        -o .test-build/test_gps_status
// ============================================================================
#include <cstdio>

#include "hal/gps_status_policy.h"

namespace hgs = hal::gpsstatus;

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

static hgs::Snapshot base() {
  hgs::Snapshot s = {};
  s.charsProcessed = 1000;
  s.msSinceLastChar = 0;
  s.locationValid = true;
  s.locationAgeMs = 0;
  s.speedValid = true;
  s.speedAgeMs = 0;
  s.courseValid = true;
  s.courseAgeMs = 0;
  return s;
}

int main() {
  std::printf("== gps_status policy host checks ==\n");

  // Healthy fix.
  {
    const hgs::Verdict v = hgs::evaluate(base());
    CHECK(v.receiving && v.valid && v.speedValid && v.courseValid, "healthy fix: all flags true");
    CHECK(v.ageMs == 0, "healthy fix: age 0");
  }
  // Bytes stop flowing → receiving drops before validity does.
  {
    hgs::Snapshot s = base();
    s.msSinceLastChar = GPS_RECEIVING_MS;
    const hgs::Verdict v = hgs::evaluate(s);
    CHECK(!v.receiving && v.valid, "byte silence exactly at receiving window: not receiving, still valid");
  }
  // Position goes stale before the byte counter does.
  {
    hgs::Snapshot s = base();
    s.locationAgeMs = GPS_STALE_MS;
    const hgs::Verdict v = hgs::evaluate(s);
    CHECK(v.receiving && !v.valid, "stale position: receiving but invalid");
  }
  // No bytes at all: nothing is valid.
  {
    hgs::Snapshot s = base();
    s.charsProcessed = 0;
    const hgs::Verdict v = hgs::evaluate(s);
    CHECK(!v.receiving && !v.valid, "zero bytes: not receiving, not valid");
  }
  // isValid() survives an unplug; the age deadline is what invalidates.
  {
    hgs::Snapshot s = base();
    s.msSinceLastChar = GPS_RECEIVING_MS + 5;
    s.locationAgeMs = GPS_STALE_MS + 5;
    const hgs::Verdict v = hgs::evaluate(s);
    CHECK(!v.receiving && !v.valid && !v.speedValid && !v.courseValid, "unplugged receiver: everything expired");
  }
  // Speed/course go stale independently while position remains fresh.
  {
    hgs::Snapshot s = base();
    s.speedAgeMs = GPS_STALE_MS + 1;
    const hgs::Verdict v = hgs::evaluate(s);
    CHECK(v.valid && !v.speedValid && v.courseValid, "speed stale only: speedValid false, courseValid true");
  }
  // Boundary: exactly at the stale deadline is still valid.
  {
    hgs::Snapshot s = base();
    s.locationAgeMs = GPS_STALE_MS - 1;
    const hgs::Verdict v = hgs::evaluate(s);
    CHECK(v.valid, "age == STALE-1 remains valid");
  }
  {
    hgs::Snapshot s = base();
    s.locationAgeMs = GPS_STALE_MS;
    const hgs::Verdict v = hgs::evaluate(s);
    CHECK(!v.valid, "age == STALE is invalid (boundary is exclusive)");
  }

  std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED",
              g_failures, g_failures == 1 ? "" : "s");
  return g_failures == 0 ? 0 : 1;
}
