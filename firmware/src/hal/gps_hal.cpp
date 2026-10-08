#include "hal/gps_hal.h"
#include "config_build.h"
#include "config_pins.h"

#if SIM_BUILD
// ============================================================================
//  SIMULATION build — NMEA generator that exercises the EXACT TinyGPSPlus
//  parsing path used on hardware. Simulated sentences never reach hardware
//  (this whole block is compiled out when SIM_BUILD=0).
// ============================================================================
#include "demo_route.h"
#include "nav_math.h"
#include "nmea_builder.h"

namespace {

class SimGpsStream : public Stream {
 public:
  int available() override {
    refill();
    return (int)count();
  }

  int read() override {
    refill();
    if (count() == 0) return -1;
    const char c = s_buf[s_tail];
    s_tail = (s_tail + 1) % sizeof(s_buf);
    return (uint8_t)c;
  }

  int peek() override {
    refill();
    if (count() == 0) return -1;
    return (uint8_t)s_buf[s_tail];
  }

  size_t write(uint8_t) override { return 1; }  // sink

 private:
  static const uint16_t kSpeedMps = 4;  // ~14 km/h along the demo route
  char s_buf[600];
  size_t s_head = 0;
  size_t s_tail = 0;
  uint32_t s_nextEmitMs = 0;
  uint8_t s_seg = 0;
  double s_progressM = 0.0;

  size_t count() const {
    if (s_head >= s_tail) return s_head - s_tail;
    return sizeof(s_buf) - (s_tail - s_head);
  }

  void push(char c) {
    const size_t next = (s_head + 1) % sizeof(s_buf);
    if (next == s_tail) return;  // full: drop (cannot happen at 1 Hz sentence rate)
    s_buf[s_head] = c;
    s_head = next;
  }

  void pushStr(const char* s) {
    while (*s != '\0') push(*s++);
  }

  void emitFix() {
    const int n = DEMO_WAYPOINT_COUNT;
    const DemoWaypoint& a = DEMO_ROUTE[s_seg];
    const DemoWaypoint& b = DEMO_ROUTE[(s_seg + 1) % n];
    const double segLen = navmath::distanceMeters(a.lat, a.lon, b.lat, b.lon);

    s_progressM += kSpeedMps;
    if (s_progressM >= segLen) {
      s_progressM -= segLen;
      s_seg = (uint8_t)((s_seg + 1) % n);
    }

    const DemoWaypoint& p0 = DEMO_ROUTE[s_seg];
    const DemoWaypoint& p1 = DEMO_ROUTE[(s_seg + 1) % n];
    const double bearing = navmath::bearingDeg(p0.lat, p0.lon, p1.lat, p1.lon);
    const double north = std::cos(navmath::toRad(bearing)) * s_progressM;
    const double east = std::sin(navmath::toRad(bearing)) * s_progressM;
    const double lat = p0.lat + navmath::toDeg(north / navmath::kEarthRadiusM);
    const double lon = p0.lon + navmath::toDeg(
        east / (navmath::kEarthRadiusM * std::cos(navmath::toRad(p0.lat))));

    const uint32_t secs = (millis() / 1000) % 86400UL;
    const unsigned hh = (unsigned)(secs / 3600);
    const unsigned mm = (unsigned)((secs / 60) % 60);
    const unsigned ss = (unsigned)(secs % 60);

    char body[100];
    char out[120];

    // GGA — position fix + altitude + satellites + HDOP
    std::snprintf(body, sizeof(body),
                  "GPGGA,%02u%02u%02u.00,%.4f,%c,%.4f,%c,1,09,0.9,920.0,M,0.0,M,,",
                  hh, mm, ss,
                  nmea::toNmeaDegrees(lat), lat >= 0 ? 'N' : 'S',
                  nmea::toNmeaDegrees(lon), lon >= 0 ? 'E' : 'W');
    if (nmea::frame(body, out, sizeof(out)) > 0) pushStr(out);

    // RMC — speed (knots) + course + date
    std::snprintf(body, sizeof(body),
                  "GPRMC,%02u%02u%02u.00,A,%.4f,%c,%.4f,%c,%.1f,%.1f,010125,,,A",
                  hh, mm, ss,
                  nmea::toNmeaDegrees(lat), lat >= 0 ? 'N' : 'S',
                  nmea::toNmeaDegrees(lon), lon >= 0 ? 'E' : 'W',
                  (double)kSpeedMps * 1.94384, bearing);
    if (nmea::frame(body, out, sizeof(out)) > 0) pushStr(out);
  }

  void refill() {
    const uint32_t now = millis();
    if ((int32_t)(now - s_nextEmitMs) >= 0) {
      emitFix();
      s_nextEmitMs = now + 1000;  // no catch-up burst after a stalled frame
    }
  }
};

SimGpsStream s_simStream;

}  // namespace

namespace hal {

void gpsBegin() {
  Serial.println(F("[SIM] GPS substitute active (checksummed NMEA generator)"));
}

Stream& gpsStream() { return s_simStream; }

}  // namespace hal

#else
// ============================================================================
//  HARDWARE build — real NEO-6M on UART2 (pins come from config_pins.h).
// ============================================================================
namespace hal {

void gpsBegin() {
  Serial2.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
}

Stream& gpsStream() { return Serial2; }

}  // namespace hal
#endif
