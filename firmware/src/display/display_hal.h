#ifndef DASHCUE_DISPLAY_HAL_H
#define DASHCUE_DISPLAY_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the panel (TFT_eSPI), the LVGL 9 display object with dual
 * DISPLAY_WIDTH x LVGL_BUF_LINES RGB565 partial-render strips, the LVGL tick
 * source, and the touch pointer input device.
 *
 * The screens and their navigation are owned by UiManager (src/ui), which also
 * owns the single task allowed to call LVGL (§8.4). This layer owns the panel,
 * the input device and the render metrics.
 */
bool display_hal_init(void);

/**
 * Switch screen rotation:
 * false = Landscape (default mount, rotation 1)
 * true  = Landscape 180 deg (rotation 3)
 */
void display_hal_set_rotation(bool landscape_180);

static inline void display_hal_set_orientation(bool inverted) {
    display_hal_set_rotation(inverted);
}

/**
 * Get current screen rotation setting.
 */
bool display_hal_get_rotation(void);

/**
 * Report one UI task iteration so the frame-time metrics are measured rather
 * than assumed. Called from the UI task loop (UiManager::step).
 */
void display_hal_record_ui_frame_ms(uint32_t frame_ms);

/**
 * Render metrics for diagnostics (GATE-03 / TC-PERF-01). Values are measured on
 * this device and are zero until the first one-second window has been folded.
 *
 * display_hal_get_fps()              full-screen-equivalent refreshes per second
 *                                    (pixels flushed / 320x240); §16 needs >= 20
 *                                    during animation
 * display_hal_get_render_px_per_s()  pixels actually pushed to the panel per second
 * display_hal_get_avg_frame_ms()     mean UI task iteration time (window of 64)
 * display_hal_get_p95_frame_ms()     slow tail of that window
 * display_hal_get_max_frame_ms()     slowest iteration seen since boot
 */
uint32_t display_hal_get_fps(void);
uint32_t display_hal_get_render_px_per_s(void);
uint32_t display_hal_get_avg_frame_ms(void);
uint32_t display_hal_get_p95_frame_ms(void);
uint32_t display_hal_get_max_frame_ms(void);

/**
 * Screen identifiers. UiManager resolves these to its own screen objects; this
 * enum is the shared vocabulary between the input handling in src/ui and the
 * screen the HAL renders.
 */
typedef enum {
    UI_SCREEN_SPLASH = 0,
    UI_SCREEN_MEDIA = 1,
    UI_SCREEN_NAV = 2,
    UI_SCREEN_SETTINGS = 3,
    UI_SCREEN_PAIRING = 4,
    UI_SCREEN_CALIBRATION = 5,
    UI_SCREEN_SAFEMODE = 6,
    UI_SCREEN_DIAG = 7,
    UI_SCREEN_DIAGNOSTICS = 7
} UiScreenId;

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_DISPLAY_HAL_H
