// ============================================================================
//  UI — Five screens (HOME / NAV / MEDIA / SYS / DASHBOARD), direct Adafruit_GFX
//  rendering on a 320x240 landscape panel. Only changed dynamic regions repaint.
// ============================================================================
#include "ui/screens.h"
#include "ui/theme.h"
#include "config_build.h"
#include "demo_route.h"
#include "nav_math.h"
#include "hal/display_hal.h"
#include "services/gps_service.h"
#include "services/media_service.h"
#include "services/nav_service.h"
#include "services/system_service.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ui {
namespace {

using namespace theme;

Adafruit_ILI9341* s_disp = nullptr;
uint8_t s_screen = SCREEN_HOME;
uint32_t s_nextDynMs = 0;   // 2 Hz dynamic areas
uint32_t s_nextSlowMs = 0;  // 1 Hz slow areas
bool s_navDrawn = false, s_mediaDrawn = false, s_chipDrawn = false, s_sysValid = false;
svc::NavState s_drawnNav = {};
svc::MediaInfo s_drawnMedia = {};
int s_drawnProgress = -1;
uint8_t s_drawnChip = 255;

// Per-region dirty bits for the nav-derived dynamic areas. Taking the diff
// region-by-region lets a 1 Hz drive-time tick repaint one small box instead
// of the whole dashboard (measured against the fillRect pixel counts below).
enum : uint8_t {
  D_DASH_SPEED  = 1 << 0,
  D_DASH_HEADING= 1 << 1,
  D_DASH_TRIP   = 1 << 2,
  D_DASH_MAX    = 1 << 3,
  D_DASH_TIME   = 1 << 4,
  D_DASH_AVG    = 1 << 5,
  D_NAV_LEFT    = 1 << 6,   // maneuver card
  D_NAV_RIGHT   = 1 << 7,   // telemetry card
};
// Banner bits need a second byte because the three nav cards plus six dash
// fields exceed eight flags.
enum : uint8_t { D_NAV_BANNER = 1 << 0, D_NAV_WP = 1 << 1 };

struct NavDirty {
  uint8_t dash;
  uint8_t nav;
};

// Diff current nav state against the last drawn snapshot, then commit the
// snapshot. Called exactly once per dynamic update; the visible screen is the
// only consumer (showScreen() invalidates on every switch).
NavDirty takeNavDirty() {
  const svc::NavState n = svc::navState();
  NavDirty out = {0xFF, 0xFF};  // structural draw: everything dirty
  if (s_navDrawn) {
    const svc::NavState& p = s_drawnNav;
    uint8_t dash = 0, nav = 0;
    if (n.navigating != p.navigating || n.speedValid != p.speedValid ||
        n.currentSpeedKmph != p.currentSpeedKmph) dash |= D_DASH_SPEED;
    if (n.headingValid != p.headingValid || n.currentHeadingDeg != p.currentHeadingDeg)
      dash |= D_DASH_HEADING;
    if (n.tripDistanceKm != p.tripDistanceKm) dash |= D_DASH_TRIP;
    if (n.maxSpeedKmph != p.maxSpeedKmph) dash |= D_DASH_MAX;
    if (n.driveTimeS != p.driveTimeS) dash |= D_DASH_TIME;
    if (n.avgSpeedKmph != p.avgSpeedKmph) dash |= D_DASH_AVG;
    if (n.navigating != p.navigating || n.turnIcon != p.turnIcon ||
        n.distanceToNextM != p.distanceToNextM ||
        strcmp(n.distanceStr, p.distanceStr) || strcmp(n.turnText, p.turnText))
      nav |= D_NAV_LEFT;
    if (n.speedValid != p.speedValid || n.headingValid != p.headingValid ||
        n.currentSpeedKmph != p.currentSpeedKmph || n.currentHeadingDeg != p.currentHeadingDeg ||
        n.source != p.source || n.bearingToNextDeg != p.bearingToNextDeg ||
        strcmp(n.etaStr, p.etaStr))
      nav |= D_NAV_RIGHT;
    if (n.navigating != p.navigating || n.source != p.source ||
        strcmp(n.streetName, p.streetName))
      nav |= D_NAV_BANNER;
    if (n.waypointIndex != p.waypointIndex || n.waypointCount != p.waypointCount)
      nav |= D_NAV_WP | D_NAV_BANNER;
    out.dash = dash;
    out.nav = nav;
  }
  s_drawnNav = n;
  s_navDrawn = true;
  return out;
}

Adafruit_ILI9341& D() { return *s_disp; }

// ---------------------------------------------------------------------------
//  helpers
// ---------------------------------------------------------------------------
bool hit(int x, int y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void text(int x, int y, const char* s, uint16_t color, uint8_t size) {
  D().setTextSize(size);
  D().setTextColor(color);
  D().setCursor(x, y);
  D().print(s);
}

void textf(int x, int y, uint16_t color, uint8_t size, const char* fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  text(x, y, buf, color, size);
}

void textFit(int x, int y, int width, const char* value, uint16_t color, uint8_t size, bool center = false) {
  char buf[80];
  const size_t capacity = static_cast<size_t>(width / (6 * size));
  const size_t limit = capacity < sizeof(buf) - 1 ? capacity : sizeof(buf) - 1;
  size_t length = strlen(value);
  if (length > limit) length = limit;
  memcpy(buf, value, length);
  buf[length] = '\0';
  if (strlen(value) > limit && length >= 3) memcpy(buf + length - 3, "...", 3);
  text(center ? x + (width - static_cast<int>(length) * 6 * size) / 2 : x, y, buf, color, size);
}

void textCentered(int cx, int y, const char* s, uint16_t color, uint8_t size) {
  textFit(cx - 146, y, 292, s, color, size, true);
}

void panel(int x, int y, int w, int h, uint16_t bg, uint16_t border) {
  D().fillRoundRect(x, y, w, h, 6, bg);
  D().drawRoundRect(x, y, w, h, 6, border);
}

// ---------------------------------------------------------------------------
//  geometric icons
// ---------------------------------------------------------------------------
void iconSpeedo(int cx, int cy, uint16_t c) {
  D().drawCircle(cx, cy, 10, c);
  D().drawLine(cx, cy, cx + 5, cy - 6, c);
  D().fillCircle(cx, cy, 2, c);
}

void iconArrow(int cx, int cy, uint16_t c) {
  D().fillTriangle(cx, cy - 9, cx - 8, cy + 7, cx + 8, cy + 7, c);
}

void iconNote(int cx, int cy, uint16_t c) {
  D().fillCircle(cx - 4, cy + 6, 5, c);
  D().fillRect(cx + 1, cy - 10, 3, 16, c);
  D().fillRect(cx + 1, cy - 12, 9, 4, c);
}

void iconGear(int cx, int cy, uint16_t c) {
  D().drawCircle(cx, cy, 8, c);
  D().drawCircle(cx, cy, 3, c);
  for (int a = 0; a < 360; a += 60) {
    const float r = (float)a * 0.0174532925f;
    D().drawLine(cx + (int)(std::cos(r) * 8.0f), cy + (int)(std::sin(r) * 8.0f),
                 cx + (int)(std::cos(r) * 12.0f), cy + (int)(std::sin(r) * 12.0f), c);
  }
}

// ---------------------------------------------------------------------------
//  status bar (present on every screen)
// ---------------------------------------------------------------------------
void drawStatusShell(const char* title) {
  D().fillRect(0, 0, SCREEN_W, STATUS_H, SURFACE);
  text(6, 7, title, TEXT, 1);

  text(118, 7, SIM_BUILD ? "SIM" : "HW", MUTED, 1);

  // Theme Indicator
  if (theme::isNightMode()) {
    text(142, 7, "NIGHT", WARN, 1);
  } else {
    text(142, 7, "DAY", ACCENT, 1);
  }
}

void drawGpsChip() {
  const svc::GpsFix g = svc::gpsFix();
  const svc::NavState n = svc::navState();
  const uint8_t state = n.source == svc::NavSource::Phone ? 3 : g.valid ? 2 : g.receiving ? 1 : 0;
  if (s_chipDrawn && state == s_drawnChip) return;
  s_chipDrawn = true;
  s_drawnChip = state;
  const uint16_t c = state >= 2 ? GOOD : state == 1 ? WARN : BAD;
  const char* label = state == 3 ? "EXTERNAL CUE" : state == 2 ? "GPS FIX" : state == 1 ? "WAIT FIX" : "NO GPS DATA";
  D().fillRect(196, 3, 118, 16, SURFACE);
  D().fillCircle(203, 11, 4, c);
  text(212, 7, label, c, 1);
}

void drawBackButton() {
  panel(2, STATUS_H + 2, 38, 38, SURFACE, ACCENT);
  D().fillTriangle(30, STATUS_H + 10, 14, STATUS_H + 18, 30, STATUS_H + 26, ACCENT);
}

bool backHit(int x, int y) {
  // Visual chevron panel is 38x38; the touch target is padded to 48x48 to
  // meet the 44 px minimum-target guideline (Fitts's law) without changing
  // the drawn UI. Must not swallow adjacent buttons — checked per screen.
  return hit(x, y, 0, STATUS_H, 48, 48);
}

// ---------------------------------------------------------------------------
//  HOME SCREEN
// ---------------------------------------------------------------------------
struct HomeBtn {
  int y;
  const char* label;
  uint8_t icon;
};
const HomeBtn kHomeBtns[] = {
    {36,  "DASHBOARD",  0},
    {86,  "NAVIGATION", 1},
    {136, "MEDIA",      2},
    {186, "SYSTEM",     3},
};
const int kHomeBtnCount = (int)(sizeof(kHomeBtns) / sizeof(kHomeBtns[0]));

void drawHome() {
  D().fillScreen(BG);
  drawStatusShell("CYD HEAD-UNIT");

  for (int i = 0; i < kHomeBtnCount; i++) {
    const HomeBtn& b = kHomeBtns[i];
    const uint16_t accent = b.icon == 1 ? GOOD : b.icon == 3 ? WARN : ACCENT;
    panel(12, b.y, SCREEN_W - 24, 44, SURFACE, accent);
    const int cx = 38, cy = b.y + 22;
    if (b.icon == 0) iconSpeedo(cx, cy, accent);
    else if (b.icon == 1) iconArrow(cx, cy, accent);
    else if (b.icon == 2) iconNote(cx, cy, accent);
    else iconGear(cx, cy, accent);
    text(70, b.y + 14, b.label, TEXT, 2);
  }
  drawGpsChip();
}

// ---------------------------------------------------------------------------
//  DASHBOARD / SPEEDOMETER SCREEN
// ---------------------------------------------------------------------------
const char* headingCardinal(float deg) {
  if (deg < 0) deg += 360.0f;
  if (deg >= 337.5f || deg < 22.5f) return "N";
  if (deg >= 22.5f && deg < 67.5f) return "NE";
  if (deg >= 67.5f && deg < 112.5f) return "E";
  if (deg >= 112.5f && deg < 157.5f) return "SE";
  if (deg >= 157.5f && deg < 202.5f) return "S";
  if (deg >= 202.5f && deg < 247.5f) return "SW";
  if (deg >= 247.5f && deg < 292.5f) return "W";
  return "NW";
}

void drawDashStatic() {
  D().fillScreen(BG);
  drawStatusShell("DASHBOARD");
  drawBackButton();

  // Speedometer central frame
  panel(44, 30, SCREEN_W - 56, 110, SURFACE, ACCENT);
  text(SCREEN_W - 80, 110, "km/h", MUTED, 2);
  // Average-speed readout (below the heading box) — avg is computed by the
  // trip computer but was previously never displayed.
  text(222, 80, "AVG", MUTED, 1);

  // Trip statistics row
  panel(12, 146, 94, 46, SURFACE, MUTED);
  text(18, 150, "TRIP", MUTED, 1);
  text(82, 172, "km", MUTED, 1);

  panel(112, 146, 96, 46, SURFACE, MUTED);
  text(118, 150, "MAX SPD", MUTED, 1);
  text(182, 172, "kph", MUTED, 1);

  panel(214, 146, 94, 46, SURFACE, MUTED);
  text(220, 150, "TIME", MUTED, 1);

  // Bottom action bar
  panel(12, 198, 142, 34, SURFACE, GOOD);
  textCentered(83, 208, "SWITCH NAV", TEXT, 1);

  panel(166, 198, 142, 34, SURFACE, WARN);
  textCentered(237, 208, "RESET TRIP", TEXT, 1);
}

void updateDashDyn() {
  const NavDirty dirty = takeNavDirty();
  if (!dirty.dash) return;

  // 1. Digital Speed readout
  if (dirty.dash & D_DASH_SPEED) {
    D().fillRect(50, 40, 170, 75, SURFACE);
    const svc::NavState n = svc::navState();
    if (n.speedValid) {
      const int spd = (int)lround(n.currentSpeedKmph);
      char buf[8];
      snprintf(buf, sizeof(buf), "%d", spd);
      // Draw large speed
      textCentered(135, 48, buf, ACCENT, 7);
    } else {
      textCentered(135, 62, "---", BAD, 5);
    }
  }

  // Heading & Cardinal indicator
  if (dirty.dash & D_DASH_HEADING) {
    const svc::NavState n = svc::navState();
    D().fillRect(SCREEN_W - 100, 40, 80, 36, SURFACE);
    if (n.headingValid) {
      textf(SCREEN_W - 90, 42, TEXT, 2, "%s", headingCardinal(n.currentHeadingDeg));
      textf(SCREEN_W - 90, 60, MUTED, 1, "%.0f deg", n.currentHeadingDeg);
    } else {
      text(SCREEN_W - 90, 48, "NO COURSE", BAD, 1);
    }
  }

  // Average speed (new readout, own region)
  if (dirty.dash & D_DASH_AVG) {
    const svc::NavState n = svc::navState();
    D().fillRect(220, 92, 80, 16, SURFACE);
    textf(222, 94, TEXT, 2, "%.1f", n.avgSpeedKmph);
  }

  // 2. Trip distance
  if (dirty.dash & D_DASH_TRIP) {
    const svc::NavState n = svc::navState();
    D().fillRect(18, 166, 62, 18, SURFACE);
    textf(18, 168, TEXT, 2, "%.1f", n.tripDistanceKm);
  }

  // 3. Max Speed
  if (dirty.dash & D_DASH_MAX) {
    const svc::NavState n = svc::navState();
    D().fillRect(118, 166, 62, 18, SURFACE);
    textf(118, 168, TEXT, 2, "%.0f", n.maxSpeedKmph);
  }

  // 4. Drive Time
  if (dirty.dash & D_DASH_TIME) {
    const svc::NavState n = svc::navState();
    D().fillRect(220, 166, 82, 18, SURFACE);
    const uint32_t t = n.driveTimeS;
    textf(220, 170, TEXT, 1, "%02lu:%02lu:%02lu",
          (unsigned long)(t / 3600), (unsigned long)((t / 60) % 60), (unsigned long)(t % 60));
  }
}

// ---------------------------------------------------------------------------
//  SMART NAVIGATION HUD (Phone-Assisted Turn-by-Turn Guidance)
// ---------------------------------------------------------------------------
void drawTurnArrowGraphic(int cx, int cy, svc::TurnIcon icon, uint16_t color) {
  switch (icon) {
    case svc::TurnIcon::TurnLeft:
      // Vertical stem up, bend 90 deg left, arrow head pointing left
      D().fillRect(cx - 2, cy + 2, 8, 16, color);
      D().fillRect(cx - 16, cy - 6, 22, 8, color);
      D().fillTriangle(cx - 24, cy - 2, cx - 14, cy - 12, cx - 14, cy + 8, color);
      break;
    case svc::TurnIcon::TurnRight:
      // Vertical stem up, bend 90 deg right, arrow head pointing right
      D().fillRect(cx - 6, cy + 2, 8, 16, color);
      D().fillRect(cx - 6, cy - 6, 22, 8, color);
      D().fillTriangle(cx + 24, cy - 2, cx + 14, cy - 12, cx + 14, cy + 8, color);
      break;
    case svc::TurnIcon::SlightLeft:
      D().fillRect(cx, cy + 6, 8, 14, color);
      D().drawLine(cx + 4, cy + 6, cx - 12, cy - 8, color);
      D().drawLine(cx + 5, cy + 6, cx - 11, cy - 8, color);
      D().fillTriangle(cx - 20, cy - 12, cx - 8, cy - 18, cx - 12, cy - 2, color);
      break;
    case svc::TurnIcon::SlightRight:
      D().fillRect(cx - 8, cy + 6, 8, 14, color);
      D().drawLine(cx - 4, cy + 6, cx + 12, cy - 8, color);
      D().drawLine(cx - 3, cy + 6, cx + 13, cy - 8, color);
      D().fillTriangle(cx + 20, cy - 12, cx + 8, cy - 18, cx + 12, cy - 2, color);
      break;
    case svc::TurnIcon::UTurn:
      // U-turn arc pointing down; mask with the card surface, not BG (the card
      // is filled with SURFACE — a BG mask would leave a black rectangle).
      D().drawCircle(cx, cy - 4, 14, color);
      D().drawCircle(cx, cy - 4, 13, color);
      D().fillRect(cx - 16, cy - 4, 32, 16, SURFACE);
      D().fillRect(cx + 6, cy - 4, 8, 18, color);
      D().fillRect(cx - 14, cy - 4, 8, 14, color);
      D().fillTriangle(cx - 10, cy + 18, cx - 18, cy + 8, cx - 2, cy + 8, color);
      break;
    case svc::TurnIcon::Roundabout:
      D().drawCircle(cx, cy, 14, color);
      D().drawCircle(cx, cy, 12, color);
      D().fillTriangle(cx, cy - 18, cx - 6, cy - 10, cx + 6, cy - 10, color);
      break;
    case svc::TurnIcon::Arrive:
      // Destination Pin — hole mask uses the card SURFACE for the same reason.
      D().fillCircle(cx, cy - 6, 10, color);
      D().fillCircle(cx, cy - 6, 4, SURFACE);
      D().fillTriangle(cx - 7, cy - 3, cx + 7, cy - 3, cx, cy + 16, color);
      break;
    default: // Straight / Ahead
      D().fillRect(cx - 4, cy - 4, 8, 22, color);
      D().fillTriangle(cx, cy - 20, cx - 14, cy - 2, cx + 14, cy - 2, color);
      break;
  }
}

void drawNavStatic() {
  D().fillScreen(BG);
  drawStatusShell("NAVIGATION");
  drawBackButton();

  // Left Maneuver / Turn Card
  panel(42, 28, 150, 152, SURFACE, ACCENT);

  // Right Instrument / Speed Telemetry Card
  panel(198, 28, 114, 152, SURFACE, MUTED);
  text(206, 34, "SPEED", MUTED, 1);
  text(282, 64, "km/h", MUTED, 1);
  text(206, 82, "HEADING", MUTED, 1);
  text(206, 124, "ETA / BEARING", MUTED, 1);

  // Bottom Street / Maneuver Name Card
  panel(12, 186, SCREEN_W - 24, 48, SURFACE, GOOD);
}

void updateNavDyn() {
  const NavDirty dirty = takeNavDirty();
  if (!dirty.nav) return;
  const svc::NavState n = svc::navState();

  // 1. Maneuver Icon & Distance (Left Card: 44..190, 30..178)
  if (dirty.nav & D_NAV_LEFT) {
    D().fillRect(44, 30, 146, 148, SURFACE);

    if (n.navigating) {
      if (n.turnIcon != svc::TurnIcon::None) drawTurnArrowGraphic(117, 60, n.turnIcon, ACCENT);
      else textFit(48, 54, 138, "TARGET", ACCENT, 2, true);

      // Distance string (e.g. "250 m" or "1.2 km")
      if (n.distanceStr[0] != '\0') {
        textFit(48, 100, 138, n.distanceStr, TEXT, strlen(n.distanceStr) <= 7 ? 3 : 2, true);
      } else {
        textf(72, 100, TEXT, 3, "%.0fm", n.distanceToNextM);
      }

      // Maneuver text (e.g. "TURN LEFT")
      if (n.turnText[0] != '\0') {
        textCentered(117, 136, n.turnText, GOOD, 1);
      } else {
        textCentered(117, 136, "GUIDANCE ACTIVE", GOOD, 1);
      }
    } else {
      // Waiting for a position or explicit external cue
      drawTurnArrowGraphic(117, 56, svc::TurnIcon::Straight, MUTED);
      textCentered(117, 92, "READY", WARN, 2);
      textCentered(117, 120, "WAITING FOR", MUTED, 1);
      textCentered(117, 134, "GPS FIX / CUE", MUTED, 1);
    }
  }

  // 2. Right Telemetry Card
  if (dirty.nav & D_NAV_RIGHT) {
    D().fillRect(202, 44, 78, 34, SURFACE);
    if (n.speedValid) {
      const int spd = (int)lround(n.currentSpeedKmph);
      char buf[8];
      snprintf(buf, sizeof(buf), "%d", spd);
      textCentered(238, 44, buf, ACCENT, 4);
    } else {
      textCentered(238, 48, "--", MUTED, 3);
    }

    // Heading & Cardinal
    D().fillRect(202, 94, 106, 24, SURFACE);
    if (n.headingValid)
      textf(206, 94, TEXT, 1, "%.0f deg (%s)", n.currentHeadingDeg, headingCardinal(n.currentHeadingDeg));
    else text(206, 94, "--", MUTED, 1);

    // ETA / Trip Distance
    D().fillRect(202, 136, 106, 36, SURFACE);
    if (n.etaStr[0] != '\0') {
      textFit(206, 138, 100, n.etaStr, TEXT, 1);
    } else {
      if (n.source == svc::NavSource::Gps) textf(206, 138, TEXT, 1, "%.0f deg", n.bearingToNextDeg);
      else text(206, 138, "--", MUTED, 1);
    }
  }

  // 3. Bottom Street Banner (14..306, 188..232)
  if (dirty.nav & D_NAV_BANNER) {
    D().fillRect(14, 188, SCREEN_W - 28, 44, SURFACE);
    if (n.navigating) {
      if (n.streetName[0] != '\0') {
        textFit(24, 194, 272, n.streetName, TEXT, 2);
      } else {
        text(24, 194, "WAYPOINT", TEXT, 2);
      }
      if (n.source == svc::NavSource::Phone) {
        text(24, 214, "External cue (10s timeout)", MUTED, 1);
      } else {
        // Route progress makes the looping waypoint list legible at a glance.
        textf(24, 214, MUTED, 1, "Waypoint %d/%d  -  direct bearing",
              n.waypointIndex + 1, n.waypointCount);
      }
    } else {
      text(24, 194, "NO ACTIVE ROUTE", WARN, 2);
      text(24, 214, "Wait for GPS or send CUE via serial", MUTED, 1);
    }
  }
}

// ---------------------------------------------------------------------------
//  MEDIA SCREEN (Bluetooth AVRCP Player)
// ---------------------------------------------------------------------------
void drawMediaStatic() {
  D().fillScreen(BG);
  drawStatusShell("MEDIA");
  drawBackButton();

  panel(12, 44, SCREEN_W - 24, 58, SURFACE, MUTED);   // Track card
  D().fillRect(12, 112, SCREEN_W - 24, 10, SURFACE);  // Progress bar background
  D().drawRect(12, 112, SCREEN_W - 24, 10, SURFACE2);

  panel(12, 128, 92, 52, SURFACE, ACCENT);   // PREV
  panel(114, 128, 92, 52, SURFACE, GOOD);    // PLAY/PAUSE
  panel(216, 128, 92, 52, SURFACE, ACCENT);  // NEXT
  textCentered(58, 146, "PREV", TEXT, 2);
  textCentered(262, 146, "NEXT", TEXT, 2);

  panel(12, 192, 40, 36, SURFACE, MUTED);   // VOL -
  panel(252, 192, 40, 36, SURFACE, MUTED);  // VOL +
  textCentered(32, 202, "-", TEXT, 2);
  textCentered(272, 202, "+", TEXT, 2);
  text(60, 206, "VOL", MUTED, 1);
  D().drawRect(84, 202, 164, 16, MUTED);
}

const char* mediaStateStr(svc::MediaState s) {
  switch (s) {
    case svc::MediaState::Playing: return "playing";
    case svc::MediaState::Paused: return "paused";
    default: return "stopped";
  }
}

void updateMediaDyn() {
  const svc::MediaInfo m = svc::mediaInfo();
  const int progress = m.durationMs ? constrain(static_cast<int>(
      static_cast<uint64_t>(m.positionMs) * 294 / m.durationMs), 0, 294) : 0;
  if (!s_mediaDrawn || m.title != s_drawnMedia.title || m.connected != s_drawnMedia.connected) {
    D().fillRect(14, 46, SCREEN_W - 28, 54, SURFACE);
    textCentered(160, 52, m.title, TEXT, 2);
    textCentered(160, 78, m.artist, MUTED, 1);
  }
  if (!s_mediaDrawn || m.state != s_drawnMedia.state) {
    D().fillRect(138, 140, 44, 28, SURFACE);
    if (m.state == svc::MediaState::Playing) {
      D().fillRect(151, 142, 7, 24, GOOD);
      D().fillRect(163, 142, 7, 24, GOOD);
    } else D().fillTriangle(150, 140, 150, 168, 172, 154, GOOD);
  }
  if (!s_mediaDrawn || progress != s_drawnProgress) {
    D().fillRect(13, 113, 294, 8, SURFACE);
    if (progress > 0) D().fillRect(13, 113, progress, 8, GOOD);
  }
  if (!s_mediaDrawn || m.volume != s_drawnMedia.volume) {
    D().fillRect(85, 203, 162, 14, SURFACE);
    if (m.volume > 0) D().fillRect(85, 203, m.volume * 162 / 10, 14, ACCENT);
  }
  if (!s_mediaDrawn || m.state != s_drawnMedia.state || m.volume != s_drawnMedia.volume) {
    D().fillRect(12, 230, SCREEN_W - 24, 10, BG);
    textf(12, 230, MUTED, 1, "%s | %s | VOL %d/10",
          SIM_BUILD ? "SIM PLAYER" : "LOCAL TONES", mediaStateStr(m.state), m.volume);
  }
  s_drawnMedia = m;
  s_drawnProgress = progress;
  s_mediaDrawn = true;
}

// ---------------------------------------------------------------------------
//  SYSTEM DIAGNOSTICS SCREEN
// ---------------------------------------------------------------------------
const int kSysRows = 9;
const char* const kSysLabels[kSysRows] = {
    "Theme", "Build", "GPS", "NMEA chars", "Display", "Audio", "Heap", "Uptime", "Firmware"};

// Cached rendered rows: the baseline erased and redrew all nine value cells
// every second even when nothing changed. Cache text + colour and touch only
// rows that actually differ (steady state: zero SPI writes for this screen).
char s_sysText[kSysRows][24] = {};
uint16_t s_sysColor[kSysRows] = {};

void drawSysStatic() {
  D().fillScreen(BG);
  drawStatusShell("SYSTEM");
  drawBackButton();
  for (int i = 0; i < kSysRows; ++i) text(48, 50 + i * 16, kSysLabels[i], MUTED, 1);
  panel(12, 194, 296, 44, SURFACE, ACCENT);
  textCentered(160, 211, "TOGGLE DAY / NIGHT", TEXT, 2);
}

void updateSysDyn() {
  const svc::GpsFix g = svc::gpsFix();
  const svc::SystemInfo si = svc::systemInfo();
  char row[kSysRows][24];
  uint16_t color[kSysRows];

  snprintf(row[0], sizeof(row[0]), "%s", theme::isNightMode() ? "Night" : "Day");
  color[0] = ACCENT;
  snprintf(row[1], sizeof(row[1]), "%s", SIM_BUILD ? "SIMULATION" : "HARDWARE");
  color[1] = TEXT;
  snprintf(row[2], sizeof(row[2]), "%s", g.valid ? "FIX" : g.receiving ? "Waiting for fix" : "No data");
  color[2] = g.valid ? GOOD : WARN;
  snprintf(row[3], sizeof(row[3]), "%lu", static_cast<unsigned long>(g.charsProcessed));
  color[3] = MUTED;
  snprintf(row[4], sizeof(row[4]), "ILI9341 320x240");
  color[4] = TEXT;
  snprintf(row[5], sizeof(row[5]), "%s", SIM_BUILD ? "SIM log" : AUDIO_ENABLED ? "GPIO25 tones" : "Disabled");
  color[5] = MUTED;
  snprintf(row[6], sizeof(row[6]), "%lu KB", static_cast<unsigned long>(si.freeHeap / 1024));
  color[6] = TEXT;
  snprintf(row[7], sizeof(row[7]), "%lu s", static_cast<unsigned long>(si.uptimeS));
  color[7] = TEXT;
  snprintf(row[8], sizeof(row[8]), "v%s", si.fwVersion);
  color[8] = MUTED;

  for (int i = 0; i < kSysRows; ++i) {
    if (s_sysValid && !strcmp(row[i], s_sysText[i]) && color[i] == s_sysColor[i]) continue;
    D().fillRect(120, 50 + i * 16, 194, 10, BG);
    text(120, 50 + i * 16, row[i], color[i], 1);
    snprintf(s_sysText[i], sizeof(s_sysText[i]), "%s", row[i]);
    s_sysColor[i] = color[i];
  }
  s_sysValid = true;
}

// ---------------------------------------------------------------------------
//  screen switching
// ---------------------------------------------------------------------------
void showScreen(uint8_t id) {
  s_navDrawn = s_mediaDrawn = s_chipDrawn = false;
  s_sysValid = false;  // BG repaint erases cached diagnostic rows
  s_screen = (id <= SCREEN_DASH) ? id : SCREEN_HOME;
  switch (s_screen) {
    case SCREEN_HOME:
      drawHome();
      break;
    case SCREEN_NAV:
      drawNavStatic();
      updateNavDyn();
      break;
    case SCREEN_MEDIA:
      drawMediaStatic();
      updateMediaDyn();
      break;
    case SCREEN_SYS:
      drawSysStatic();
      updateSysDyn();
      break;
    case SCREEN_DASH:
      drawDashStatic();
      updateDashDyn();
      break;
  }
  drawGpsChip();
}

}  // namespace

// ---------------------------------------------------------------------------
//  public API
// ---------------------------------------------------------------------------
void uiBegin() {
  s_disp = &hal::gfx();
  showScreen(SCREEN_HOME);
}

void uiTick() {
  const uint32_t now = millis();

  if ((int32_t)(now - s_nextDynMs) >= 0) {
    s_nextDynMs = now + 400;
    drawGpsChip();
    if (s_screen == SCREEN_NAV) updateNavDyn();
    else if (s_screen == SCREEN_MEDIA) updateMediaDyn();
    else if (s_screen == SCREEN_DASH) updateDashDyn();
  }

  if ((int32_t)(now - s_nextSlowMs) >= 0) {
    s_nextSlowMs = now + 1000;
    if (s_screen == SCREEN_SYS) updateSysDyn();
  }
}

void uiOnTap(int x, int y) {
  // Tap the status-bar theme indicator to toggle Day/Night mode. Target is
  // padded beyond the 22 px bar (research: minimum ~44 px touch targets) so
  // the control stays fat-finger friendly without changing the layout.
  if (hit(x, y, 130, 0, 70, 32)) {
    theme::toggleNightMode();
    showScreen(s_screen);
    return;
  }

  // GPS status chip: from HOME it acts as a shortcut into NAVIGATION; on
  // other screens it stays inert so an accidental brush never navigates away.
  if (s_screen == SCREEN_HOME && hit(x, y, 200, 0, 120, 30)) {
    showScreen(SCREEN_NAV);
    return;
  }

  switch (s_screen) {
    case SCREEN_HOME:
      for (int i = 0; i < kHomeBtnCount; i++) {
        const HomeBtn& b = kHomeBtns[i];
        if (hit(x, y, 12, b.y, SCREEN_W - 24, 44)) {
          if (i == 0) showScreen(SCREEN_DASH);
          else if (i == 1) showScreen(SCREEN_NAV);
          else if (i == 2) showScreen(SCREEN_MEDIA);
          else showScreen(SCREEN_SYS);
          return;
        }
      }
      break;

    case SCREEN_DASH:
      if (backHit(x, y)) showScreen(SCREEN_HOME);
      // Action-bar targets padded from 142x34 to 150x44 (visuals unchanged).
      else if (hit(x, y, 8, 194, 150, 44)) showScreen(SCREEN_NAV);
      else if (hit(x, y, 162, 194, 150, 44)) {
        svc::navResetTrip();
        updateDashDyn();
      }
      break;

    case SCREEN_NAV:
      if (backHit(x, y)) showScreen(SCREEN_HOME);
      break;

    case SCREEN_MEDIA:
      if (backHit(x, y)) showScreen(SCREEN_HOME);
      else if (hit(x, y, 12, 128, 92, 52)) svc::mediaPrev();
      else if (hit(x, y, 114, 128, 92, 52)) svc::mediaToggle();
      else if (hit(x, y, 216, 128, 92, 52)) svc::mediaNext();
      // Volume targets padded from 40x36 to 48x44; slider starts at x=84 so
      // the enlarged zones (…x=56 / x=248…) still do not overlap it.
      else if (hit(x, y, 8, 188, 48, 44)) svc::mediaVolumeDown();
      else if (hit(x, y, 248, 188, 48, 44)) svc::mediaVolumeUp();
      break;

    case SCREEN_SYS:
      if (backHit(x, y)) showScreen(SCREEN_HOME);
      else if (hit(x, y, 12, 194, 296, 44)) {
        theme::toggleNightMode();
        showScreen(SCREEN_SYS);
      }
      break;
  }
  if (s_screen == SCREEN_MEDIA) updateMediaDyn();
}

void uiShowScreen(uint8_t id) { showScreen(id); }
uint8_t uiCurrentScreen() { return s_screen; }

}  // namespace ui
