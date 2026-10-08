#!/usr/bin/env bash
# ============================================================================
#  CYD-Build — host test runner (no hardware, no PlatformIO required).
#
#  Runs the four host builds that verify the pure firmware logic:
#    1. nav_math / nmea_builder                (nav_nmea_host_test.cpp)
#    2. nav/media services + console + cues    (services_host_test.cpp, SIM_BUILD=0)
#    3. same suite against the simulation build (services_host_test.cpp, SIM_BUILD=1)
#    4. GPS staleness/reception policy         (gps_status_host_test.cpp)
#
#  Requires: g++ (C++11). Binaries land in .test-build/ (gitignored).
#  Windows: use run-tests.cmd (same suites).
# ============================================================================
set -u
cd "$(dirname "$0")"
mkdir -p .test-build

fail=0
declare -a RESULTS=()

run_suite() {
  local name="$1"; shift
  # shellcheck disable=SC2086
  if g++ -std=c++11 "$@"; then
    local bin; bin=".test-build/${name}.exe"
    if "${bin}"; then
      RESULTS+=("${name}: PASS")
    else
      echo "[RUN FAIL] ${name}"
      RESULTS+=("${name}: RUN FAIL")
      fail=1
    fi
  else
    echo "[BUILD FAIL] ${name}"
    RESULTS+=("${name}: BUILD FAIL")
    fail=1
  fi
}

run_suite test_nav -Ifirmware/include \
  firmware/test/nav_nmea_host_test.cpp

run_suite services_hw -DSIM_BUILD=0 -Ifirmware/include -Ifirmware/test/host \
  firmware/test/services_host_test.cpp \
  firmware/src/services/nav_service.cpp \
  firmware/src/services/media_service.cpp \
  firmware/src/demo_route.cpp

run_suite services_sim -DSIM_BUILD=1 -Ifirmware/include -Ifirmware/test/host \
  firmware/test/services_host_test.cpp \
  firmware/src/services/nav_service.cpp \
  firmware/src/services/media_service.cpp \
  firmware/src/demo_route.cpp

run_suite test_gps_status -Ifirmware/include \
  firmware/test/gps_status_host_test.cpp

echo
echo "---- per-suite results ----"
for r in "${RESULTS[@]}"; do echo "  $r"; done
echo
if [ "$fail" -ne 0 ]; then
  echo "HOST TESTS: FAILED"
else
  echo "HOST TESTS: ALL PASSED"
fi
exit "$fail"
