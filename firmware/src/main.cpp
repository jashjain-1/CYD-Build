// ============================================================================
//  CYD-Build — entry point
//
//  HARDWARE build (default, SIM_BUILD=0): real ILI9341 + XPT2046 drivers,
//  real NEO-6M NMEA on UART2, optional LEDC tone output. No synthetic data
//  is compiled into this build.
//
//  SIM build (SIM_BUILD=1): substitutes are compiled in ONLY in the HAL
//  layers, preserving every software interface.
// ============================================================================
#include <Arduino.h>
#include <cstdio>
#include <cstring>

#include "config_build.h"
#include "console_input.h"
#include "phone_cue.h"
#include "config_pins.h"
#include "hal/audio_hal.h"
#include "hal/display_hal.h"
#include "hal/gps_hal.h"
#include "hal/touch_hal.h"
#include "services/gps_service.h"
#include "services/media_service.h"
#include "services/nav_service.h"
#include "services/system_service.h"
#include "ui/screens.h"
#include "ui/theme.h"

namespace {

uint32_t s_nextNavMs = 0;
bool s_touchTest = false;
ConsoleLine s_console;

void printStatus() {
  const svc::GpsFix g = svc::gpsFix();
  const svc::NavState n = svc::navState();
  const svc::MediaInfo m = svc::mediaInfo();
  const svc::SystemInfo si = svc::systemInfo();

  Serial.println(F("---- STATUS ----"));
  Serial.printf("build       : %s\n", (SIM_BUILD != 0) ? "SIMULATION" : "HARDWARE");
  Serial.printf("firmware    : %s v%s\n", si.fwName, si.fwVersion);
  Serial.printf("uptime      : %lu s   free heap: %lu bytes\n",
                (unsigned long)si.uptimeS, (unsigned long)si.freeHeap);
  Serial.printf("gps         : %s / %s   chars: %lu   sats: %lu   hdop: %.1f\n",
                g.receiving ? "receiving" : "NO DATA",
                g.valid ? "FIX" : "no fix",
                (unsigned long)g.charsProcessed, (unsigned long)g.sats, g.hdop);
  if (g.valid) {
    Serial.printf("position    : %.5f, %.5f   speed %.1f km/h   course %.0f deg\n",
                  g.lat, g.lon, g.speedKmph, g.courseDeg);
  }
  Serial.printf("trip        : %.2f km   max: %.0f km/h   avg: %.1f km/h   drive time: %lu s\n",
                n.tripDistanceKm, n.maxSpeedKmph, n.avgSpeedKmph, (unsigned long)n.driveTimeS);
  if (n.waypointCount > 0) {
    Serial.printf("waypoint    : %d/%d  '%s'  %.0f m to target\n",
                  (n.waypointIndex % n.waypointCount) + 1, n.waypointCount,
                  n.streetName, (double)n.distanceToNextM);
  }
  Serial.printf("nav         : %s   cue: %s (%s)   street: '%s'   speed: %.1f km/h\n",
                n.navigating ? "active" : "inactive", n.turnText, n.distanceStr,
                n.streetName, n.currentSpeedKmph);

  const char* media = "stopped";
  if (m.state == svc::MediaState::Playing) media = "playing";
  else if (m.state == svc::MediaState::Paused) media = "paused";
  Serial.printf("media       : %s   '%s'   vol %d/10   bt: %s\n", media, m.title, m.volume,
                m.connected ? "CONNECTED" : "disconnected");
  Serial.printf("screen      : %u   theme: %s\n", (unsigned)ui::uiCurrentScreen(),
                theme::isNightMode() ? "NIGHT" : "DAY");
  Serial.printf("gps raw echo: %s\n", svc::gpsRawEcho() ? "on" : "off");
}

void processCommand(const char* cmd) {
  if (!strcmp(cmd, "STATUS")) {
    printStatus();
  } else if (!strcmp(cmd, "TOUCHTEST")) {
    s_touchTest = !s_touchTest;
    Serial.printf("[CMD] touch test %s\n", s_touchTest ? "ON (taps print, UI paused)" : "OFF");
  } else if (!strcmp(cmd, "GPSRAW")) {
    const bool on = !svc::gpsRawEcho();
    svc::gpsSetRawEcho(on);
    Serial.printf("[CMD] GPS raw echo %s\n", on ? "ON" : "OFF");
  } else if (!strcmp(cmd, "HOME")) {
    ui::uiShowScreen(ui::SCREEN_HOME);
  } else if (!strcmp(cmd, "DASH")) {
    ui::uiShowScreen(ui::SCREEN_DASH);
  } else if (!strcmp(cmd, "NAV")) {
    ui::uiShowScreen(ui::SCREEN_NAV);
  } else if (!strncmp(cmd, "CUE ", 4)) {
    phonecue::Cue cue = {};
    if (phonecue::parse(cmd + 4, cue) &&
        svc::navSetPhoneCue(cue.icon, cue.distance, cue.street, cue.eta, cue.speed)) {
      Serial.println(F("[CMD] external cue accepted (expires in 10 s)"));
    } else {
      Serial.println(F("[CMD] invalid cue: CUE LEFT|250 m|MG Road|12 min|40"));
    }
  } else if (!strcmp(cmd, "MEDIA")) {
    ui::uiShowScreen(ui::SCREEN_MEDIA);
  } else if (!strcmp(cmd, "SYS")) {
    ui::uiShowScreen(ui::SCREEN_SYS);
  } else if (!strcmp(cmd, "THEME")) {
    theme::toggleNightMode();
    ui::uiShowScreen(ui::uiCurrentScreen());
    Serial.printf("[CMD] theme toggled: %s\n", theme::isNightMode() ? "NIGHT" : "DAY");
  } else if (!strcmp(cmd, "TRIPRESET")) {
    svc::navResetTrip();
    Serial.println(F("[CMD] trip odometer reset"));
  } else if (!strcmp(cmd, "BT")) {
#if SIM_BUILD
    svc::simMediaSetConnected(!svc::mediaIsConnected());
    Serial.printf("[CMD] BT simulated connection: %s\n", svc::mediaIsConnected() ? "CONNECTED" : "DISCONNECTED");
#else
    Serial.println(F("[CMD] BT is simulation-only; no Bluetooth transport"));
#endif
  } else if (!strcmp(cmd, "NAVRESET")) {
    svc::navReset();
    Serial.println(F("[CMD] nav reset"));
  } else if (!strcmp(cmd, "HELP") || !strcmp(cmd, "?")) {
    Serial.println(F("[CMD] STATUS | TOUCHTEST | GPSRAW | HOME | DASH | NAV | MEDIA | SYS"));
    Serial.println(F("[CMD] CUE <ICON>|<dist>|<street>|<eta>|<speed> | THEME | TRIPRESET"));
    Serial.println(F("[CMD] NAVRESET | BT | TAP x y (SIM) | HELP"));
  } else if (!strncmp(cmd, "TAP ", 4)) {
#if SIM_BUILD
    int x = 0, y = 0;
    char extra = 0;
    if (sscanf(cmd + 4, "%d %d %c", &x, &y, &extra) == 2 && x >= 0 && x < 320 && y >= 0 && y < 240) {
      hal::simTouchInject(x, y);
      Serial.printf("[CMD] tap injected at %d,%d\n", x, y);
    } else Serial.println(F("[CMD] expected TAP x y within 320x240"));
#else
    Serial.println(F("[CMD] TAP is simulation-only"));
#endif
  } else {
    Serial.println(F("[CMD] unknown. Try: HELP | STATUS | TOUCHTEST | HOME | DASH | NAV | CUE TURN|DIST|STREET|ETA|SPEED | MEDIA | SYS | THEME | TRIPRESET | BT | NAVRESET | TAP x y"));
  }
}

void handleConsole() {
  for (unsigned bytes = 0; bytes < 64 && Serial.available() > 0; ++bytes) {
    const ConsoleLine::Result result = s_console.push(static_cast<char>(Serial.read()));
    if (result == ConsoleLine::Ready) processCommand(s_console.text());
    else if (result == ConsoleLine::Overflow)
      Serial.println(F("[CMD] line too long; discarded"));
  }
}

}  // namespace

void setup() {
  Serial.begin(CONSOLE_BAUD);
  delay(50);  // boot-time only; the main loop never blocks
  Serial.println();
  Serial.println(F("=== CYD Head-Unit ==="));
  Serial.printf("fw %s | build: %s\n", FW_VERSION,
                (SIM_BUILD != 0) ? "SIMULATION" : "HARDWARE");

  Serial.print(F("[init] display... "));
  hal::displayBegin();
  Serial.println(F("ok"));

  Serial.print(F("[init] touch... "));
  hal::touchBegin();
  Serial.println(F("ok"));

  Serial.print(F("[init] gps... "));
  hal::gpsBegin();
  Serial.println(F("ok"));

  Serial.print(F("[init] audio... "));
  hal::audioBegin();
  Serial.println(F("ok"));

  svc::gpsBegin();
  svc::navBegin();
  svc::mediaBegin();
  svc::systemBegin();
  ui::uiBegin();

  Serial.println(F("Ready. Cmds: STATUS | TOUCHTEST | GPSRAW | HOME | DASH | NAV | MEDIA | SYS | THEME | TRIPRESET | NAVRESET | CUE ..."));
}

void loop() {
  const uint32_t now = millis();

  svc::gpsUpdate();

  if ((int32_t)(now - s_nextNavMs) >= 0) {
    s_nextNavMs = now + 1000;  // guidance at 1 Hz
    svc::navUpdate(svc::gpsFix());
  }

  svc::mediaTick();
  svc::systemTick();
  handleConsole();

  int x = 0, y = 0;
  if (hal::touchPollTap(x, y)) {
    if (s_touchTest) {
      Serial.printf("[TOUCH] x=%d y=%d\n", x, y);
    } else {
      ui::uiOnTap(x, y);
    }
  }

  ui::uiTick();
  delay(1);  // yield to the scheduler between bounded input/render work
}
