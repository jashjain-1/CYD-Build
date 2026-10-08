#include "services/gps_service.h"
#include "config_build.h"
#include "hal/gps_hal.h"
#include "hal/gps_status_policy.h"
#include <TinyGPSPlus.h>

namespace {
TinyGPSPlus s_gps;
uint32_t s_lastChars = 0;
uint32_t s_lastCharsMs = 0;
bool s_rawEcho = false;
}  // namespace

namespace svc {

void gpsBegin() {
  // Streams are created by the HAL; nothing else to do here.
}

void gpsUpdate() {
  Stream& stream = hal::gpsStream();
  // Bound work even if a misbehaving input sends continuously.
  for (unsigned bytes = 0; bytes < 256 && stream.available() > 0; ++bytes) {
    const char c = (char)stream.read();
    if (s_rawEcho) Serial.write(c);  // GPSRAW diagnostic: prove the wiring
    s_gps.encode(c);
  }

  const uint32_t chars = s_gps.charsProcessed();
  if (chars != s_lastChars) {
    s_lastChars = chars;
    s_lastCharsMs = millis();
  }
}

GpsFix gpsFix() {
  GpsFix f = {};
  const uint32_t now = millis();

  // Reception/validity policy lives in hal/gps_status_policy.h (host-tested).
  hal::gpsstatus::Snapshot snap;
  snap.charsProcessed = s_gps.charsProcessed();
  snap.msSinceLastChar = now - s_lastCharsMs;
  snap.locationValid = s_gps.location.isValid();
  snap.locationAgeMs = s_gps.location.age();
  snap.speedValid = s_gps.speed.isValid();
  snap.speedAgeMs = s_gps.speed.age();
  snap.courseValid = s_gps.course.isValid();
  snap.courseAgeMs = s_gps.course.age();
  const hal::gpsstatus::Verdict v = hal::gpsstatus::evaluate(snap);

  f.charsProcessed = snap.charsProcessed;
  f.receiving = v.receiving;
  f.valid = v.valid;
  f.speedValid = v.speedValid;
  f.courseValid = v.courseValid;
  f.ageMs = v.ageMs;

  if (snap.locationValid) {
    f.lat = s_gps.location.lat();
    f.lon = s_gps.location.lng();
  }
  if (f.valid) {
    if (f.speedValid) f.speedKmph = s_gps.speed.kmph();
    if (f.courseValid) f.courseDeg = s_gps.course.deg();
  }
  if (s_gps.satellites.isValid()) f.sats = s_gps.satellites.value();
  if (s_gps.hdop.isValid()) f.hdop = s_gps.hdop.hdop();
  return f;
}

void gpsSetRawEcho(bool on) { s_rawEcho = on; }
bool gpsRawEcho() { return s_rawEcho; }

}  // namespace svc
