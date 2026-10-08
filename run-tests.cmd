@echo off
REM ============================================================================
REM  CYD-Build — host test runner (no hardware, no PlatformIO required).
REM
REM  Runs the four host builds that verify the pure firmware logic:
REM    1. nav_math / nmea_builder                (nav_nmea_host_test.cpp)
REM    2. nav/media services + console + cues    (services_host_test.cpp, SIM_BUILD=0)
REM    3. same suite against the simulation build (services_host_test.cpp, SIM_BUILD=1)
REM    4. GPS staleness/reception policy         (gps_status_host_test.cpp)
REM
REM  Requires: g++ (MinGW) on PATH. Tested with MinGW.org GCC 6.3.0.
REM  Binaries land in .test-build\ (gitignored).
REM  NOTE: on some locked-down machines endpoint policy intermittently blocks
REM  freshly written .exe files; simply re-run if a suite you expect to pass
REM  is blocked.
REM
REM  Linux/macOS: run run-tests.sh (same suites).
REM ============================================================================
setlocal enabledelayedexpansion
mkdir .test-build 2>nul

set /a FAIL=0
set /a RAN=0

g++ -std=c++11 -Ifirmware/include firmware/test/nav_nmea_host_test.cpp -o .test-build\test_nav.exe
if errorlevel 1 (
  echo [BUILD FAIL] nav_nmea
  set /a FAIL+=1
) else (
  .\.test-build\test_nav.exe
  if errorlevel 1 (echo [RUN FAIL] nav_nmea & set /a FAIL+=1)
  set /a RAN+=1
)

g++ -std=c++11 -DSIM_BUILD=0 -Ifirmware/include -Ifirmware/test/host firmware/test/services_host_test.cpp firmware/src/services/nav_service.cpp firmware/src/services/media_service.cpp firmware/src/demo_route.cpp -o .test-build\services_hw.exe
if errorlevel 1 (
  echo [BUILD FAIL] services_hw
  set /a FAIL+=1
) else (
  .\.test-build\services_hw.exe
  if errorlevel 1 (echo [RUN FAIL] services_hw & set /a FAIL+=1)
  set /a RAN+=1
)

g++ -std=c++11 -DSIM_BUILD=1 -Ifirmware/include -Ifirmware/test/host firmware/test/services_host_test.cpp firmware/src/services/nav_service.cpp firmware/src/services/media_service.cpp firmware/src/demo_route.cpp -o .test-build\services_sim.exe
if errorlevel 1 (
  echo [BUILD FAIL] services_sim
  set /a FAIL+=1
) else (
  .\.test-build\services_sim.exe
  if errorlevel 1 (echo [RUN FAIL] services_sim & set /a FAIL+=1)
  set /a RAN+=1
)

g++ -std=c++11 -Ifirmware/include firmware/test/gps_status_host_test.cpp -o .test-build\test_gps_status.exe
if errorlevel 1 (
  echo [BUILD FAIL] gps_status
  set /a FAIL+=1
) else (
  .\.test-build\test_gps_status.exe
  if errorlevel 1 (echo [RUN FAIL] gps_status & set /a FAIL+=1)
  set /a RAN+=1
)

echo.
if !FAIL! GTR 0 (
  echo HOST TESTS: FAILED
) else (
  if !RAN! LSS 4 (
    echo HOST TESTS: INCOMPLETE ^(only !RAN!/4 suites reported^) — re-run
  ) else (
    echo HOST TESTS: ALL PASSED
  )
)
exit /b !FAIL!
