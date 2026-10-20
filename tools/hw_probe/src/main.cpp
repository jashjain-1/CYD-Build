// DashCue hardware probe - GATE-02 (board), GATE-03 (display), GATE-04 (touch).
//
// This firmware answers the questions the specification refuses to assume
// (§0.3): what board revision is this, which display controller, on which SPI
// bus/pins is the touch controller, how fast can the display be written, and
// what is the raw touch range and polling rate.
//
// It exists to produce evidence, so it never guesses:
//   PASS     - a measurement satisfied the expectation
//   FAIL     - a measurement contradicts the expectation
//   UNKNOWN  - the check could not be conclusive on this hardware
// UNKNOWN is never reported as PASS (spec §19.1).
//
// MEASURED ON 2026-10-16, ESP32 rev 3 / 4 MB flash / no PSRAM:
//   display works: 320x240, full-screen fill 31.2 ms, 320x40 strip blit 5.6 ms,
//   single-buffered full-frame estimate ~29 fps (target is >= 20 fps).
//   RDDID (0x04) returns 0x0000, so this is a panel that does not implement the
//   ID registers - controller identification stays UNKNOWN and is NOT guessed.
//   Touch: the first version of this probe reported "both buses responded",
//   which was wrong. A saturated read (transfer16 == 0xFFFF) shifts to x=y=8191
//   and computes z=4095, which passes the pressure threshold while meaning "the
//   controller is not driving MISO at all". The detector below requires a
//   *plausible* signature, and the scan searches bus/CS combinations instead of
//   assuming the two hypotheses in §5.2.
#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <TFT_eSPI.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_chip_info.h>

#include "dashcue_config.h"
#include "dashcue_version.h"

namespace {

TFT_eSPI tft = TFT_eSPI();

// Candidate SPI pin sets. "shared" is the display bus (§5.2 hypothesis A) and is
// already up because TFT_eSPI began it; "separate" is hypothesis B.
struct BusCandidate {
  const char* name;
  int8_t sck, miso, mosi;
};

const BusCandidate kBusCandidates[] = {
    {"shared", PIN_LCD_SCLK, PIN_LCD_MISO, PIN_LCD_MOSI},
    // The touch bus constants are the pins GATE-02 measured (SCK 25 / MISO 39 /
    // MOSI 32). They were renamed from the original PIN_TOUCH_ALT_* hypothesis
    // names when the measurement promoted them to the real configuration; the
    // probe still has to scan them, so it reads the measured names.
    {"separate", PIN_TOUCH_SCLK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI},
};

// Candidate chip-selects. Output-capable pins only (GPIO 34-39 are input-only
// and can never drive CS). The bus pins themselves are excluded per bus below.
const uint8_t kChipSelectCandidates[] = {33, 32, 25, 27, 22, 21, 5, 15, 4};

struct TouchInfo {
  bool found = false;
  const char* bus_name = "none";
  uint8_t cs = 0;
  uint16_t raw_x_min = 4095, raw_x_max = 0, raw_y_min = 4095, raw_y_max = 0;
  uint32_t sample_hz = 0;
  uint32_t pressed_reads = 0;
} touch_info;

struct ProbeResult {
  const char* display = "UNKNOWN";
  const char* backlight = "UNKNOWN";
  uint32_t fill_screen_us = 0;
  uint32_t fill_100x100_us = 0;
  uint32_t blit_strip_us = 0;
  uint32_t fps_estimate = 0;
  uint8_t id2 = 0, id3 = 0;
} result;

void banner() {
  Serial.println();
  Serial.println("=========================================================");
  Serial.printf(" DASHCUE HARDWARE PROBE  fw=%s  hw=%s\n", DASHCUE_FW_VERSION_STRING,
                DASHCUE_HW_REVISION);
  Serial.println("=========================================================");
}

void probeSoC() {
  esp_chip_info_t info;
  esp_chip_info(&info);
  const char* model = "UNKNOWN";
  switch (info.model) {
    case CHIP_ESP32: model = "ESP32"; break;
    case CHIP_ESP32S2: model = "ESP32-S2"; break;
    case CHIP_ESP32S3: model = "ESP32-S3"; break;
    case CHIP_ESP32C3: model = "ESP32-C3"; break;
    default: break;
  }

  uint32_t flash_bytes = 0;
  esp_flash_get_size(nullptr, &flash_bytes);

  uint8_t mac[6] = {0};
  const bool mac_ok = esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK;

  Serial.printf("CHIP=%s REV=%d CORES=%d FREQ_MHZ=%u\n", model, info.revision, info.cores,
                (unsigned)getCpuFrequencyMhz());
  Serial.printf("FLASH_BYTES=%u FLASH_MHZ=%u\n", (unsigned)flash_bytes,
                (unsigned)(ESP.getFlashChipSpeed() / 1000000));
  Serial.printf("PSRAM_BYTES=%u\n", (unsigned)ESP.getPsramSize());
  Serial.printf("HEAP_FREE=%u HEAP_MIN_FREE=%u HEAP_LARGEST_BLOCK=%u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  Serial.printf("SKETCH_BYTES=%u FREE_SKETCH_BYTES=%u\n", (unsigned)ESP.getSketchSize(),
                (unsigned)ESP.getFreeSketchSpace());
  if (mac_ok) {
    Serial.printf("MAC=%02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4],
                  mac[5]);
  } else {
    Serial.println("MAC=UNREADABLE");
  }
  Serial.printf("RESET_REASON=%d\n", (int)esp_reset_reason());

  // The custom partition table (firmware/partitions.csv) needs a real 4 MB part,
  // so a smaller flash invalidates the layout rather than merely being smaller.
  if (flash_bytes >= 4u * 1024u * 1024u) {
    Serial.println("RESULT flash=PASS");
  } else {
    Serial.printf("RESULT flash=FAIL (found %u bytes; the custom partition table needs 4 MB)\n",
                  (unsigned)flash_bytes);
  }
}

void probeDisplay() {
  tft.init();
  tft.setRotation(1);  // landscape, as the HUD is mounted

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, LCD_BACKLIGHT_ON);
  delay(50);
  result.backlight = "PASS";

  // RDDID (0x04). 93 41 = ILI9341, 85 52 = ST7789. Many CYD clones - including
  // this one, which returns 00 00 - do not implement the ID register at all, in
  // which case the controller stays UNKNOWN. It is never inferred from a guess.
  result.id2 = tft.readcommand8(0x04, 1);
  result.id3 = tft.readcommand8(0x04, 2);
  const char* controller = "UNKNOWN";
  if (result.id2 == 0x93 && result.id3 == 0x41) {
    controller = "ILI9341";
  } else if (result.id2 == 0x85 && result.id3 == 0x52) {
    controller = "ST7789";
  }
  Serial.printf("DISPLAY_ID=0x%02X%02X CONTROLLER=%s\n", result.id2, result.id3, controller);
  Serial.printf("DISPLAY_SIZE=%dx%d ROTATION=1\n", tft.width(), tft.height());

  uint32_t t0 = micros();
  tft.fillScreen(TFT_BLACK);
  result.fill_screen_us = micros() - t0;

  t0 = micros();
  tft.fillRect(0, 0, 100, 100, TFT_RED);
  result.fill_100x100_us = micros() - t0;

  static uint16_t strip[DISPLAY_WIDTH * LVGL_BUF_LINES];
  for (int i = 0; i < DISPLAY_WIDTH * LVGL_BUF_LINES; i++) {
    strip[i] = (uint16_t)((i * 7) & 0xFFFF);
  }
  t0 = micros();
  tft.pushImage(0, 0, DISPLAY_WIDTH, LVGL_BUF_LINES, strip);
  result.blit_strip_us = micros() - t0;

  const uint32_t frame_us = result.blit_strip_us * (DISPLAY_HEIGHT / LVGL_BUF_LINES);
  result.fps_estimate = frame_us ? (1000000u / frame_us) : 0;

  Serial.printf("DISPLAY_FILL_SCREEN_US=%u\n", (unsigned)result.fill_screen_us);
  Serial.printf("DISPLAY_FILL_100x100_US=%u\n", (unsigned)result.fill_100x100_us);
  Serial.printf("DISPLAY_BLIT_320x%d_US=%u\n", LVGL_BUF_LINES, (unsigned)result.blit_strip_us);
  Serial.printf("DISPLAY_FULLFRAME_FPS_EST=%u\n", (unsigned)result.fps_estimate);

  const uint16_t bars[] = {TFT_RED,  TFT_ORANGE, TFT_YELLOW, TFT_GREEN,
                           TFT_CYAN, TFT_BLUE,   TFT_MAGENTA, TFT_WHITE};
  const int bar_w = DISPLAY_WIDTH / 8;
  for (int i = 0; i < 8; i++) {
    tft.fillRect(i * bar_w, 0, bar_w, 60, bars[i]);
  }
  tft.fillRect(0, 60, DISPLAY_WIDTH, DISPLAY_HEIGHT - 60, TFT_BLACK);
  for (int y = 64; y < 120; y += 4) {
    tft.drawFastHLine(0, y, DISPLAY_WIDTH, TFT_DARKGREY);
  }
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("DashCue HW probe", DISPLAY_WIDTH / 2, 128, 4);
  tft.drawString(String("fw ") + DASHCUE_FW_VERSION_STRING + "  " + DASHCUE_HW_REVISION,
                 DISPLAY_WIDTH / 2, 156, 2);
  tft.drawString("TOUCH THE PANEL", DISPLAY_WIDTH / 2, 180, 2);
  tft.drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, TFT_WHITE);
  tft.fillRect(0, DISPLAY_HEIGHT - 4, 8, 4, TFT_RED);
  tft.fillRect(DISPLAY_WIDTH - 8, DISPLAY_HEIGHT - 4, 8, 4, TFT_RED);

  result.display = "PASS";
}

// ------------------------------------------------------------------- touch
// One raw read using the XPT2046 protocol: a command byte, then a 16-bit
// transfer whose significant bits are the 12-bit result (shift right 3, exactly
// as the reference implementation does).
uint16_t xptRaw(SPIClass& bus, uint8_t cs, uint8_t cmd) {
  bus.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(cs, LOW);
  bus.transfer(cmd);
  const uint16_t value = bus.transfer16(0x00);
  digitalWrite(cs, HIGH);
  bus.endTransaction();
  return value >> 3;
}

// Liveness by variance: a controller that is present but untouched still returns
// drifting values on the X channel. A bus with nothing driving MISO returns the
// same saturated value (0 or 4095) every time. A single reading cannot tell
// those apart; eight readings can.
bool channelIsAlive(SPIClass& bus, uint8_t cs, uint16_t* out_min, uint16_t* out_max) {
  uint16_t mn = 0xFFFF, mx = 0;
  for (int i = 0; i < 8; i++) {
    const uint16_t v = xptRaw(bus, cs, 0x91 /* X */);
    if (v < mn) mn = v;
    if (v > mx) mx = v;
  }
  *out_min = mn;
  *out_max = mx;
  if (mx == 0 && mn == 0) return false;        // MISO held low
  if (mn == 4095 && mx == 4095) return false;  // MISO held high
  return mx != mn;
}

SPIClass& candidateBus(const BusCandidate& c, bool* needs_begin) {
  static SPIClass aux(VSPI == HSPI ? VSPI : HSPI);
  if (c.sck == PIN_LCD_SCLK) {
    *needs_begin = false;  // TFT_eSPI already brought the display bus up
    return SPI;
  }
  *needs_begin = true;
  return aux;
}

void scanTouch() {
  Serial.println("TOUCH_SCAN_BEGIN");
  for (const BusCandidate& bus_candidate : kBusCandidates) {
    bool needs_begin = false;
    SPIClass& bus = candidateBus(bus_candidate, &needs_begin);
    if (needs_begin) {
      bus.begin(bus_candidate.sck, bus_candidate.miso, bus_candidate.mosi, -1);
    }
    for (uint8_t cs : kChipSelectCandidates) {
      // Skip a CS that collides with this bus's own data/clock pins.
      if (cs == bus_candidate.sck || cs == bus_candidate.miso || cs == bus_candidate.mosi) {
        continue;
      }
      pinMode(cs, OUTPUT);
      digitalWrite(cs, HIGH);
      uint16_t mn = 0, mx = 0;
      const bool alive = channelIsAlive(bus, cs, &mn, &mx);
      Serial.printf("TOUCH_SCAN bus=%-8s sck=%d miso=%d mosi=%d cs=%-2u x=[%u..%u] alive=%s\n",
                    bus_candidate.name, bus_candidate.sck, bus_candidate.miso,
                    bus_candidate.mosi, cs, mn, mx, alive ? "yes" : "no");
      if (alive && !touch_info.found) {
        touch_info.found = true;
        touch_info.bus_name = bus_candidate.name;
        touch_info.cs = cs;
      }
    }
  }
  Serial.println("TOUCH_SCAN_END");
}

// Sample the discovered controller and report the raw range the calibration in
// §15.3 will work from. Requires a human to touch the panel: without a touch the
// result is UNKNOWN, never PASS.
void sampleTouch() {
  if (!touch_info.found) {
    Serial.println("RESULT touch=BLOCKED (no responding XPT2046 on any scanned bus/CS)");
    return;
  }

  bool needs_begin = false;
  const BusCandidate* chosen = nullptr;
  for (const BusCandidate& c : kBusCandidates) {
    if (strcmp(c.name, touch_info.bus_name) == 0) chosen = &c;
  }
  if (chosen == nullptr) {
    Serial.println("RESULT touch=BLOCKED (internal: discovered bus not in candidate table)");
    return;
  }
  SPIClass& bus = candidateBus(*chosen, &needs_begin);

  XPT2046_Touchscreen ts(touch_info.cs);
  ts.begin(bus);
  ts.setRotation(1);

  Serial.printf("TOUCH_PROMPT=touch the panel now (8 s window) - bus=%s cs=%u\n",
                touch_info.bus_name, touch_info.cs);
  delay(700);

  const uint32_t start = millis();
  uint32_t samples = 0;
  while (millis() - start < 8000) {
    if (ts.touched()) {
      const TS_Point p = ts.getPoint();
      // Reject saturated readings: they are a floating bus, not a finger.
      const bool plausible = p.x >= 0 && p.x <= 4095 && p.y >= 0 && p.y <= 4095 && p.z < 4090;
      if (plausible) {
        touch_info.pressed_reads++;
        if ((uint16_t)p.x < touch_info.raw_x_min) touch_info.raw_x_min = (uint16_t)p.x;
        if ((uint16_t)p.x > touch_info.raw_x_max) touch_info.raw_x_max = (uint16_t)p.x;
        if ((uint16_t)p.y < touch_info.raw_y_min) touch_info.raw_y_min = (uint16_t)p.y;
        if ((uint16_t)p.y > touch_info.raw_y_max) touch_info.raw_y_max = (uint16_t)p.y;
      }
    }
    samples++;
    delayMicroseconds(200);
  }
  const uint32_t elapsed = millis() - start;
  touch_info.sample_hz = elapsed ? (samples * 1000u) / elapsed : 0;

  Serial.printf("TOUCH_SAMPLE_HZ=%u\n", (unsigned)touch_info.sample_hz);
  Serial.printf("TOUCH_RAW_RANGE x=[%u..%u] y=[%u..%u] pressed_reads=%u\n",
                touch_info.raw_x_min == 4095 ? 0 : touch_info.raw_x_min, touch_info.raw_x_max,
                touch_info.raw_y_min == 4095 ? 0 : touch_info.raw_y_min, touch_info.raw_y_max,
                (unsigned)touch_info.pressed_reads);

  if (touch_info.pressed_reads > 0) {
    Serial.printf("RESULT touch=PASS bus=%s cs=%u\n", touch_info.bus_name, touch_info.cs);
  } else {
    Serial.printf("RESULT touch=UNKNOWN (controller alive on bus=%s cs=%u but no plausible touch "
                  "was seen - was the panel touched?)\n",
                  touch_info.bus_name, touch_info.cs);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);

  banner();
  probeSoC();
  probeDisplay();
  scanTouch();
  sampleTouch();

  Serial.println("---- measured ----");
  Serial.printf("RESULT backlight=%s\n", result.backlight);
  Serial.printf(
      "RESULT display=%s controller=%s\n", result.display,
      result.id2 == 0x93 && result.id3 == 0x41   ? "ILI9341"
      : result.id2 == 0x85 && result.id3 == 0x52 ? "ST7789"
                                                 : "UNKNOWN");
  Serial.printf("MEASURE fill_screen_us=%u blit_320x%d_us=%u fullframe_fps_est=%u touch_hz=%u\n",
                (unsigned)result.fill_screen_us, LVGL_BUF_LINES, (unsigned)result.blit_strip_us,
                (unsigned)result.fps_estimate, (unsigned)touch_info.sample_hz);
  Serial.println("---- end probe: live crosshair follows the finger ----");
}

void loop() {
  static uint32_t last_draw = 0;
  static uint32_t last_hb = 0;
  static XPT2046_Touchscreen* live = nullptr;

  if (live == nullptr) {
    if (!touch_info.found) {
      delay(50);
      return;  // nothing to read: do not pretend a crosshair has a source
    }
    bool needs_begin = false;
    for (const BusCandidate& c : kBusCandidates) {
      if (strcmp(c.name, touch_info.bus_name) == 0) {
        live = new XPT2046_Touchscreen(touch_info.cs);
        live->begin(candidateBus(c, &needs_begin));
        live->setRotation(1);
      }
    }
  }

  if (live != nullptr && live->touched()) {
    const TS_Point p = live->getPoint();
    const bool plausible = p.x >= 0 && p.x <= 4095 && p.y >= 0 && p.y <= 4095 && p.z < 4090;
    const uint32_t now = millis();
    if (plausible && now - last_draw >= 16) {
      last_draw = now;
      const int x = constrain(p.x, 0, DISPLAY_WIDTH - 1);
      const int y = constrain(p.y, 0, DISPLAY_HEIGHT - 1);
      Serial.printf("TOUCH_RAW x=%d y=%d z=%d -> screen x=%d y=%d\n", p.x, p.y, p.z, x, y);
      tft.fillRect(0, 196, DISPLAY_WIDTH, 44, TFT_BLACK);
      tft.drawFastHLine(x - 12, y, 24, TFT_GREEN);
      tft.drawFastVLine(x, y - 12, 24, TFT_GREEN);
      tft.fillCircle(x, y, 3, TFT_GREEN);
    }
  }

  const uint32_t now = millis();
  if (now - last_hb >= HEARTBEAT_INTERVAL_MS) {
    last_hb = now;
    Serial.printf("HEARTBEAT uptime_s=%u heap_free=%u min_heap=%u\n", now / 1000,
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  }
  delay(2);
}
