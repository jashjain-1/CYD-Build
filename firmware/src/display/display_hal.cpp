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

// Dual partial-render strip buffers (DEC-03, §16 memory plan):
// two DISPLAY_WIDTH x LVGL_BUF_LINES RGB565 strips in internal SRAM. LVGL draws
// into one while the other is being pushed to the panel, which is what keeps a
// refresh from stalling the UI task for the whole frame.
static lv_color16_t s_disp_buf1[DISPLAY_WIDTH * LVGL_BUF_LINES];
static lv_color16_t s_disp_buf2[DISPLAY_WIDTH * LVGL_BUF_LINES];
static_assert(sizeof(s_disp_buf1) + sizeof(s_disp_buf2) <= 12800,
              "CYD draw buffers must stay inside the 12,800-byte SRAM budget");

lv_display_t *s_display = NULL;
lv_indev_t *s_indev = NULL;
bool s_rotation_180 = false;

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

    s_tft.startWrite();
    s_tft.setAddrWindow(area->x1, area->y1, w, h);
    s_tft.pushColors((uint16_t *)px_map, w * h, true);
    s_tft.endWrite();

    s_flush_px_in_second += w * h;

    lv_display_flush_ready(disp);
}

void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    int16_t x = 0, y = 0;
    bool pressed = touch_hal_get_point(&x, &y);
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

bool display_hal_init(void) {
    // Bring up the panel driver first: a TFT_eSPI configuration that does not
    // match the panel is the one failure this layer cannot detect in software
    // (this board does not answer RDDID - see docs/HARDWARE_REPORT.md).
    log_heap("pre_tft");
    s_tft.init();
    log_heap("tft_init");
    s_tft.setRotation(1); // Landscape (spec §12.9)
    s_tft.invertDisplay(true); // Panel color inversion: corrects inverted black/white and cyan/red
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

    lv_display_set_buffers(s_display, s_disp_buf1, s_disp_buf2, sizeof(s_disp_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    log_heap("set_buffers");
    lv_display_set_flush_cb(s_display, display_flush_cb);
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);

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
    lv_indev_set_scroll_limit(s_indev, 16);

    // The screens themselves belong to UiManager (src/ui): it owns navigation,
    // the tab bar and the only LVGL-driving task. This layer owns the panel, the
    // input device and the render metrics.
    log_heap("display+indev");
    Serial.println("DISPLAY ready (320x240 landscape, RGB565, dual strip buffers)");
    return true;
}

void display_hal_set_rotation(bool landscape_180) {
    s_rotation_180 = landscape_180;
    s_tft.setRotation(landscape_180 ? 3 : 1);
    touch_hal_set_rotation(landscape_180);
}

bool display_hal_get_rotation(void) {
    return s_rotation_180;
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
void display_hal_set_rotation(bool landscape_180) { (void)landscape_180; }
bool display_hal_get_rotation(void) { return false; }
void display_hal_record_ui_frame_ms(uint32_t frame_ms) { (void)frame_ms; }
uint32_t display_hal_get_fps(void) { return 0; }
uint32_t display_hal_get_render_px_per_s(void) { return 0; }
uint32_t display_hal_get_avg_frame_ms(void) { return 0; }
uint32_t display_hal_get_p95_frame_ms(void) { return 0; }
uint32_t display_hal_get_max_frame_ms(void) { return 0; }

#endif // ESP_PLATFORM || ARDUINO
