#include "display_hal.h"
#include "dashcue_config.h"
#include "touch/touch_hal.h"
#include "board/board_hal.h"

#include <string.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

TFT_eSPI s_tft = TFT_eSPI();

// Single partial-render strip buffer (DEC-03, §16 memory plan): one
// DISPLAY_WIDTH x LVGL_BUF_LINES RGB565 strip in internal SRAM.
// 480 x 10 x 2 = 9,600 bytes. Accommodated safely by the DRAM reclaimed from
// classic Bluetooth audio removal.
static lv_color16_t s_disp_buf1[DISPLAY_WIDTH * LVGL_BUF_LINES];
static_assert(sizeof(s_disp_buf1) <= 9600,
              "CYD 480-wide draw buffer must stay inside the 9,600-byte SRAM budget");


lv_display_t *s_display = NULL;
lv_indev_t *s_indev = NULL;
bool s_rotation_180 = false;
bool s_ready = false;

// ------------------------------------------------------------------ metrics
// Measured, never assumed: the UI task reports its own iteration time, and the
// flush callback counts the pixels actually pushed to the panel. Pixels per
// second is the number that matters for §16's ">= 20 FPS during animation":
// 20 FPS of full-screen 320x240 RGB565 updates is 1.536 Mpx/s.
uint32_t s_ui_frames_in_second = 0;
uint32_t s_ui_frame_times[64] = {0};
size_t s_ui_frame_time_count = 0;
size_t s_ui_frame_time_idx = 0;
uint32_t s_ui_frame_max_ms = 0;
uint32_t s_ui_avg_frame_ms = 0;
uint32_t s_ui_p95_frame_ms = 0;

uint32_t s_flush_px_in_second = 0;
uint32_t s_render_px_per_s = 0;
uint32_t s_fps_equiv = 0; // full-screen-equivalent refreshes per second
uint32_t s_last_metrics_ms = 0;

// LVGL's time base. UiManager::step() is the only LVGL caller (§8.4) and it runs
// lv_timer_handler(); without a tick source LVGL's internal time never advances,
// so every LVGL timer, animation and scroll - the passkey countdown and the
// splash hold included - silently does nothing. LVGL 9 asks for the tick through
// a callback, so the UI task cannot forget to call lv_tick_inc().
uint32_t dashcue_tick_cb(void) {
    return (uint32_t)millis();
}

// Print heap at the stages of display bring-up: a display that cannot come up
// should say why on the console instead of only asserting on the panel.
void log_heap(const char *stage) {
    Serial.printf("HEAP after %-16s free=%u largest=%u min=%u\n",
                  stage,
                  (unsigned)esp_get_free_heap_size(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned)esp_get_minimum_free_heap_size());
    Serial.flush();
}

void display_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    // Mutex arbitration for shared SPI bus with XPT2046 touch controller
    if (g_spi_bus_mutex) {
        if (xSemaphoreTake(g_spi_bus_mutex, portMAX_DELAY) == pdTRUE) {
            s_tft.pushImage(area->x1, area->y1, w, h, (uint16_t *)px_map);
#if defined(ESP_PLATFORM) || defined(ARDUINO)
            s_tft.dmaWait(); // Ensure any in-flight DMA transmission completes before releasing bus
#endif
            xSemaphoreGive(g_spi_bus_mutex);
        }
    } else {
        s_tft.pushImage(area->x1, area->y1, w, h, (uint16_t *)px_map);
    }

    s_flush_px_in_second += w * h;

    lv_display_flush_ready(disp);
}

void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    int16_t x = 0, y = 0;
    bool pressed = touch_hal_get_point(&x, &y);

    if (x < 0) x = 0;
    else if (x >= DISPLAY_WIDTH) x = DISPLAY_WIDTH - 1;

    if (y < 0) y = 0;
    else if (y >= DISPLAY_HEIGHT) y = DISPLAY_HEIGHT - 1;

    data->point.x = x;
    data->point.y = y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// Fold one UI task iteration into the metrics window; once a second, publish.
void record_ui_frame(uint32_t frame_ms, uint32_t now) {
    s_ui_frame_times[s_ui_frame_time_idx] = frame_ms;
    s_ui_frame_time_idx = (s_ui_frame_time_idx + 1) % 64;
    if (s_ui_frame_time_count < 64) ++s_ui_frame_time_count;
    if (frame_ms > s_ui_frame_max_ms) {
        s_ui_frame_max_ms = frame_ms;
    }

    s_ui_frames_in_second++;

    if (now - s_last_metrics_ms < 1000) {
        return;
    }

    s_render_px_per_s = (uint32_t)((uint64_t)s_flush_px_in_second * 1000 / (now - s_last_metrics_ms));
    s_fps_equiv = s_render_px_per_s / (uint32_t)(DISPLAY_WIDTH * DISPLAY_HEIGHT);

    uint32_t sum = 0;
    uint32_t sorted_times[64];
    for (size_t i = 0; i < s_ui_frame_time_count; i++) {
        sum += s_ui_frame_times[i];
        sorted_times[i] = s_ui_frame_times[i];
        size_t j = i;
        while (j > 0 && sorted_times[j] < sorted_times[j - 1]) {
            uint32_t tmp = sorted_times[j];
            sorted_times[j] = sorted_times[j - 1];
            sorted_times[j - 1] = tmp;
            --j;
        }
    }
    s_ui_avg_frame_ms = sum / s_ui_frame_time_count;
    // Nearest-rank percentile of actual recent samples, including startup.
    s_ui_p95_frame_ms = sorted_times[(s_ui_frame_time_count * 95 + 99) / 100 - 1];

    s_ui_frames_in_second = 0;
    s_flush_px_in_second = 0;
    s_last_metrics_ms = now;
}

} // namespace

// A GPIO that is shorted, loaded or held by another owner reads back as the
// level it is being held at, not the level we drove. That is what "a pin is
// stuck" actually looks like from firmware, and it is measurable: drive each
// panel line high, then low, and compare the input register with the drive.
// Runs before TFT_eSPI takes the pins over.
void log_panel_pin_integrity(void) {
    struct PinCheck { const char *name; int gpio; };
    const PinCheck checks[] = {
        {"CS", PIN_LCD_CS}, {"DC", PIN_LCD_DC},
        {"SCLK", PIN_LCD_SCLK}, {"MOSI", PIN_LCD_MOSI},
    };
    for (const PinCheck &c : checks) {
        pinMode(c.gpio, OUTPUT);
        digitalWrite(c.gpio, HIGH);
        delayMicroseconds(50);
        const int read_hi = digitalRead(c.gpio);
        digitalWrite(c.gpio, LOW);
        delayMicroseconds(50);
        const int read_lo = digitalRead(c.gpio);
        Serial.printf("PIN %-4s gpio=%2d drive_hi->read=%d drive_lo->read=%d %s\n",
                      c.name, c.gpio, read_hi, read_lo,
                      (read_hi == 1 && read_lo == 0) ? "ok" : "STUCK/LOADED");
    }
    // Leave the bus idle-high before the driver claims it.
    digitalWrite(PIN_LCD_CS, HIGH);
}

bool display_hal_init(void) {
    if (!g_spi_bus_mutex) {
        g_spi_bus_mutex = xSemaphoreCreateMutex();
    }
    // Bring up the panel driver first: a TFT_eSPI configuration that does not
    // match the panel is the one failure this layer cannot detect in software
    // (this board does not answer RDDID - see docs/HARDWARE_REPORT.md).
    log_panel_pin_integrity();
    log_heap("pre_tft");
    s_tft.init();
    log_heap("tft_init");
    s_tft.setRotation(1); // Landscape (spec §12.9)
    // LVGL renders RGB565 in host (little-endian) order; the panel takes it
    // big-endian. Every flush goes through pushImage, which honours this flag.
    s_tft.setSwapBytes(true);
    // Normal polarity (inversion OFF). Measured on this panel: sending
    // invertDisplay(true) turns the near-black cockpit UI into a solid white field,
    // which caused the white screen. Explicitly ensure inversion is disabled.
    s_tft.invertDisplay(false);
    s_tft.fillScreen(TFT_BLACK);
    board_set_backlight_pct(board_get_backlight_pct());
    log_heap("tft_rotation");

    lv_init();
    lv_tick_set_cb(dashcue_tick_cb);
    s_last_metrics_ms = millis();
    log_heap("lv_init");

    s_display = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (!s_display) {
        Serial.println("DISPLAY: lv_display_create failed");
        return false;
    }
    log_heap("lv_display_create");

    // NULL second buffer = single buffered: LVGL flushes the strip before drawing
    // the next one. See the note on s_disp_buf1 for why one strip is enough.
    lv_display_set_buffers(s_display, s_disp_buf1, NULL, sizeof(s_disp_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    log_heap("set_buffers");
    lv_display_set_flush_cb(s_display, display_flush_cb);
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    // Cap the refresh at ~60 Hz. LVGL 9.6 has no set_refr_period(); the period
    // lives on the display's refresher timer and defaults to 33 ms, which makes
    // scrolling and the accent fade visibly step. 16 ms matches the panel
    // budget in §16 and the UI task's own loop.
    if (lv_timer_t *refr = lv_display_get_refr_timer(s_display)) {
        lv_timer_set_period(refr, 16);
    }

    s_indev = lv_indev_create();
    if (!s_indev) {
        Serial.println("DISPLAY: lv_indev_create failed");
        return false;
    }
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, touch_read_cb);
    // Momentum after a drag. LVGL defaults to no throw, which made long lists
    // (Settings) stop dead the instant the finger lifted.
    lv_indev_set_scroll_throw(s_indev, 60);
    lv_indev_set_scroll_limit(s_indev, 40);

    // The screens themselves belong to UiManager (src/ui): it owns navigation,
    // the tab bar and the only LVGL-driving task. This layer owns the panel, the
    // input device and the render metrics.
    log_heap("display+indev");
    s_ready = true;
    Serial.println("DISPLAY ready (480x320 landscape, RGB565, single strip buffer)");
    return true;
}

bool display_hal_acquire_spi(uint32_t wait_ms) {
    if (!g_spi_bus_mutex) return true;
    return (xSemaphoreTake(g_spi_bus_mutex, pdMS_TO_TICKS(wait_ms)) == pdTRUE);
}

void display_hal_release_spi(void) {
    if (g_spi_bus_mutex) {
        xSemaphoreGive(g_spi_bus_mutex);
    }
}

void display_hal_set_rotation(bool landscape_180) {
    s_rotation_180 = landscape_180;
    s_tft.setRotation(landscape_180 ? 3 : 1);
    touch_hal_set_rotation(landscape_180);
}

bool display_hal_get_rotation(void) {
    return s_rotation_180;
}

bool display_hal_is_ready(void) {
    return s_ready;
}

void display_hal_show_boot_error(const char *stage) {
    if (!s_ready) {
        return;
    }
    // Deliberately raw TFT_eSPI: this runs when the UI task may not exist yet.
    // Same palette as the theme so a failed boot does not look like a different
    // product, and large enough to read from a driver's seat.
    s_tft.fillScreen(TFT_BLACK);
    s_tft.setTextDatum(MC_DATUM);
    s_tft.setTextColor(TFT_RED, TFT_BLACK);
    s_tft.drawString("BOOT FAILED", DISPLAY_WIDTH / 2, 96, 4);
    s_tft.setTextColor(TFT_WHITE, TFT_BLACK);
    s_tft.drawString(stage, DISPLAY_WIDTH / 2, 132, 2);
    s_tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    s_tft.drawString("see COM4 log at 115200", DISPLAY_WIDTH / 2, 164, 2);
    s_tft.drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, TFT_RED);
}

void display_hal_record_ui_frame_ms(uint32_t frame_ms) {
    record_ui_frame(frame_ms, millis());
}

uint32_t display_hal_get_fps(void) {
    return s_fps_equiv;
}

uint32_t display_hal_get_render_px_per_s(void) {
    return s_render_px_per_s;
}

uint32_t display_hal_get_avg_frame_ms(void) {
    return s_ui_avg_frame_ms;
}

uint32_t display_hal_get_p95_frame_ms(void) {
    return s_ui_p95_frame_ms;
}

uint32_t display_hal_get_max_frame_ms(void) {
    return s_ui_frame_max_ms;
}

#else

bool display_hal_init(void) { return true; }
bool display_hal_acquire_spi(uint32_t wait_ms) { (void)wait_ms; return true; }
void display_hal_release_spi(void) {}
bool display_hal_is_ready(void) { return false; }
void display_hal_show_boot_error(const char *stage) { (void)stage; }
void display_hal_set_rotation(bool landscape_180) { (void)landscape_180; }
bool display_hal_get_rotation(void) { return false; }
void display_hal_record_ui_frame_ms(uint32_t frame_ms) { (void)frame_ms; }
uint32_t display_hal_get_fps(void) { return 0; }
uint32_t display_hal_get_render_px_per_s(void) { return 0; }
uint32_t display_hal_get_avg_frame_ms(void) { return 0; }
uint32_t display_hal_get_p95_frame_ms(void) { return 0; }
uint32_t display_hal_get_max_frame_ms(void) { return 0; }

#endif // ESP_PLATFORM || ARDUINO
