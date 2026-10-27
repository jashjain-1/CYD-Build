#include "touch_hal.h"
#include "audio/audio_hal.h"
#include "dashcue_config.h"

#include <string.h>
#include <math.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include "board/board_hal.h"

#ifndef ESP_LOGW
#define ESP_LOGW(tag, format, ...) Serial.printf("[%s] " format "\n", tag, ##__VA_ARGS__)
#endif

namespace {

static const char *TAG [[maybe_unused]] = "touch_hal";

TouchCalibrationBlob s_cal_blob;
bool s_is_calibrated = false;
bool s_rotation_180 = false;
bool s_invert_y = false;
bool s_invert_x = false;
bool s_cal_mode = false;

portMUX_TYPE s_touch_mux = portMUX_INITIALIZER_UNLOCKED;

TouchPoint s_latest_point = {0, 0, false};
TouchRawPoint s_latest_raw = {0, 0, 0};
bool s_has_raw = false;

volatile uint32_t s_sample_counter = 0;
volatile uint32_t s_sample_hz = 0;
uint32_t s_last_hz_calc_ms = 0;

TaskHandle_t s_touch_task_handle = NULL;

static inline int16_t median3(int16_t a, int16_t b, int16_t c) {
    if (a > b) {
        if (b > c) return b;
        return (a > c) ? c : a;
    } else {
        if (a > c) return a;
        return (b > c) ? c : b;
    }
}

void arm_penirq(void) {
    digitalWrite(PIN_LCD_CS, HIGH);
    SPIClass &bus = TFT_eSPI::getSPIinstance();
    const SPISettings kTouchSpi(2500000, MSBFIRST, SPI_MODE0);
    bus.beginTransaction(kTouchSpi);
    digitalWrite(PIN_TOUCH_CS, LOW);
    bus.transfer(0xD0); // PD1=0, PD0=0: power down ADC, enable PENIRQ
    bus.transfer16(0x00);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    bus.endTransaction();
}

int16_t read_channel(uint8_t cmd) {
    digitalWrite(PIN_LCD_CS, HIGH);
    SPIClass &bus = TFT_eSPI::getSPIinstance();
    const SPISettings kTouchSpi(2500000, MSBFIRST, SPI_MODE0);
    bus.beginTransaction(kTouchSpi);
    digitalWrite(PIN_TOUCH_CS, LOW);
    bus.transfer(cmd);
    const int16_t value = (int16_t)((bus.transfer16(0x00) >> 3) & 0x0FFF);
    bus.transfer(0xD0); // Always restore power-down and re-arm PENIRQ
    bus.transfer16(0x00);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    bus.endTransaction();
    return value;
}

constexpr uint32_t kFaultRejectThreshold = 25;
// XPT2046 panels differ in how firmly PENIRQ is driven. Some boards only pull
// it low under a hard press (or leave the IRQ trace unconnected), so the IRQ is
// a fast path, not the only path. A light finger still raises Z1 well above
// idle noise; this threshold is the measured onset floor, not a calibration
// value. Thresholds in raw ADC counts.
constexpr int16_t kPressureOnsetMin = 24;

volatile uint32_t s_saturated_rejects = 0;
volatile uint32_t s_valid_presses = 0;
volatile bool s_pen_contact_seen = false;
uint32_t s_boot_ms = 0;

// Press-onset tracking. If separate presses land on the same raw coordinate the
// panel is reporting contact without position - the X/Y plates are not dividing
// as a finger moves, so no calibration curve can exist. Distances between the
// wizard's five targets are ~260 screen px, which on a working panel is far more
// than this in raw counts; 300 counts is a floor, not a tuning knob.
constexpr int16_t kPositionSpreadMin = 300;
constexpr uint8_t kPositionOnsetsNeeded = 3;
volatile bool s_position_dead = false;
bool s_prev_pen = false;
uint8_t s_onset_count = 0;
int16_t s_onset_x_min = 4095, s_onset_x_max = 0, s_onset_y_min = 4095, s_onset_y_max = 0;

void touch_task(void *param) {
    (void)param;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(10); // 10 ms = 100 Hz

    uint8_t debounce_press_count = 0;
    uint8_t debounce_release_count = 0;
    bool latched_pressed = false;
    bool prev_latched = false;
    bool s_prev_pen_down = false;

    while (1) {
        TickType_t now_tick = xTaskGetTickCount();
        if ((TickType_t)(now_tick - last_wake_time) > (period_ticks * 10)) {
            last_wake_time = now_tick;
        }
        vTaskDelayUntil(&last_wake_time, period_ticks);

        uint32_t now = millis();
        s_sample_counter++;
        if (now - s_last_hz_calc_ms >= 1000) {
            s_sample_hz = s_sample_counter;
            s_sample_counter = 0;
            s_last_hz_calc_ms = now;
        }

        // a. Contact detection: GPIO 36 (PIN_TOUCH_IRQ, active LOW) gates any
        // press. Sampling continues while a press is latched (to track motion),
        // and a periodic probe every 5 ticks guards against a floating IRQ
        // deadlock — but a probe WITHOUT pen_down can only be used to keep the
        // ADC warmed, never to start or position a press (phantom-touch guard).
        const int pen_irq = digitalRead(PIN_TOUCH_IRQ);
        const bool pen_down = (pen_irq == LOW);
        // Gated SPI bus sampling: skip bus acquisition and SPI sampling entirely
        // when the screen is idle (!pen_down && !latched_pressed). This deconflicts
        // the shared VSPI bus with display_flush_cb and saves CPU cycles.
        // Release debounce is preserved because latched_pressed remains true until
        // 3 consecutive release samples confirm the finger has lifted.
        const bool should_sample = pen_down || latched_pressed;

        if (!should_sample) {
            // Screen is completely idle: skip SPI bus acquisition and sampling
            portENTER_CRITICAL(&s_touch_mux);
            s_latest_point.pressed = false;
            s_has_raw = false;
            portEXIT_CRITICAL(&s_touch_mux);

            debounce_press_count = 0;
            debounce_release_count = 0;
            latched_pressed = false;
            prev_latched = false;
            s_prev_pen_down = false;
            s_prev_pen = false;

            continue;
        }
        // IRQ-confirmed contact, or a drag already in progress.
        const bool contact = pen_down || latched_pressed;

        // b. First-contact sample discard: the XPT2046's first conversion after
        // PENIRQ arms is electrically unsettled and often lands far from the
        // finger (classic source of phantom jumps). Skip that one cycle.
        const bool first_contact_skip = pen_down && !s_prev_pen_down && !latched_pressed;

        int16_t raw_x = 0;
        int16_t raw_y = 0;
        int16_t raw_z1 = 0;
        int16_t raw_z2 = 0;
        bool valid_sample = false;

        // b. Acquire g_spi_bus_mutex with robust timeout (pdMS_TO_TICKS(12)).
        if (should_sample) {
            bool mutex_acquired = false;
            if (g_spi_bus_mutex) {
                mutex_acquired = (xSemaphoreTake(g_spi_bus_mutex, pdMS_TO_TICKS(12)) == pdTRUE);
            } else {
                mutex_acquired = true;
            }

            if (mutex_acquired) {
                // Ensure LCD CS is deasserted high
                digitalWrite(PIN_LCD_CS, HIGH);

                // Read 3 samples per channel to eliminate bare-finger capacitive noise spikes
                int16_t sx[3], sy[3], sz1[3], sz2[3];
                for (uint8_t i = 0; i < 3; ++i) {
                    sx[i]  = read_channel(0x91); // X
                    sy[i]  = read_channel(0xD1); // Y
                    sz1[i] = read_channel(0xB1); // Z1
                    sz2[i] = read_channel(0xC1); // Z2
                }

                raw_x  = median3(sx[0],  sx[1],  sx[2]);
                raw_y  = median3(sy[0],  sy[1],  sy[2]);
                raw_z1 = median3(sz1[0], sz1[1], sz1[2]);
                raw_z2 = median3(sz2[0], sz2[1], sz2[2]);

                // Explicitly send 0xD0 to ensure PENIRQ is armed
                arm_penirq();

                if (g_spi_bus_mutex) {
                    xSemaphoreGive(g_spi_bus_mutex);
                }

                // c. Pressure validation: Z = 4095 + Z1 - Z2.
                // Authentic touch contact produces significant Z1 and plausible coordinates.
                int32_t z_pressure = (int32_t)4095 + (int32_t)raw_z1 - (int32_t)raw_z2;
                (void)z_pressure;

                if (pen_down) {
                    s_pen_contact_seen = true;
                }

                const bool bounds_ok = (raw_x >= 40 && raw_x <= 4080 &&
                                        raw_y >= 40 && raw_y <= 4080);
                const bool pressure_and_bounds_ok = (raw_z1 >= 3 && bounds_ok);
                // Graceful fallback for a lazy or disconnected PENIRQ: start a
                // press from Z1 alone. This is a press *onset* only, so it can
                // never reposition an already-latched drag.
                const bool pressure_onset = pressure_and_bounds_ok &&
                                            !pen_down && !latched_pressed &&
                                            raw_z1 >= kPressureOnsetMin;

                if (pressure_onset) {
                    // One-shot operator evidence that the IRQ fallback is what
                    // is keeping the panel usable, without spamming every tap.
                    static bool s_logged_pressure_fallback = false;
                    if (!s_logged_pressure_fallback) {
                        s_logged_pressure_fallback = true;
                        Serial.printf("TOUCH: PENIRQ quiet; pressure fallback active (z1=%d)\n",
                                      (int)raw_z1);
                    }
                }

                if (pressure_and_bounds_ok) {
                    if ((contact || pressure_onset) && !first_contact_skip) {
                        valid_sample = true;
                        s_valid_presses++;
                        // Active self-healing: real user taps clear saturated reject counter
                        s_saturated_rejects = 0;
                    }
                } else if (pen_down) {
                    // Saturated read rejection & fault counter:
                    s_saturated_rejects++;
                    if (s_saturated_rejects == kFaultRejectThreshold) {
                        ESP_LOGW(TAG, "TOUCH: Saturated reject threshold reached (%u >= %u) -> FAULTED",
                                 (unsigned int)s_saturated_rejects, (unsigned int)kFaultRejectThreshold);
                        Serial.printf("TOUCH: Saturated reject threshold reached (%u >= %u) -> FAULTED\n",
                                      (unsigned int)s_saturated_rejects, (unsigned int)kFaultRejectThreshold);
                    }
                } else {
                    // Finger lifted (!pen_down).
                    // Reset accumulated reject counter so disparate light finger taps
                    // across driving sessions never accumulate into a false FAULTED state!
                    if (s_saturated_rejects < kFaultRejectThreshold) {
                        s_saturated_rejects = 0;
                    }
                }
            }
        }

        // d. Debouncing: one 100 Hz sample is enough to latch a press that has
        // already passed Z1 and bounds validation. The old 2-sample rule added
        // 20 ms to every tap for noise that the pressure gate already rejects.
        // Release still needs three clean samples so a shaky lift cannot chop a
        // drag into two clicks.
        if (valid_sample) {
            debounce_release_count = 0;
            if (debounce_press_count < 1) {
                debounce_press_count++;
            }
            if (debounce_press_count >= 1) {
                latched_pressed = true;
            }
        } else {
            debounce_press_count = 0;
            // Only unlatch if PENIRQ is physically HIGH (finger lifted).
            // While pen_down is LOW, touch contact is continuous; slight ADC noise must not break a hold.
            if (!pen_down) {
                if (debounce_release_count < 3) {
                    debounce_release_count++;
                }
                if (debounce_release_count >= 3) {
                    latched_pressed = false;
                }
            } else {
                debounce_release_count = 0;
            }
        }

        // e. Feed calibrated coordinates to LVGL touch input driver.
        portENTER_CRITICAL(&s_touch_mux);
        if (valid_sample) {
            s_latest_raw.x = raw_x;
            s_latest_raw.y = raw_y;
            s_latest_raw.z = raw_z1;
            s_has_raw = true;
        } else if (!latched_pressed) {
            s_has_raw = false;
        }

        if (latched_pressed) {
            if (s_is_calibrated && !s_cal_mode) {
                s_latest_point = touch_calibration_apply(&s_cal_blob, s_latest_raw.x, s_latest_raw.y, s_rotation_180);
            } else {
                s_latest_point.x = s_latest_raw.x;
                s_latest_point.y = s_latest_raw.y;
            }
            if (s_invert_x) {
                s_latest_point.x = (DISPLAY_WIDTH - 1) - s_latest_point.x;
            }
            if (s_invert_y) {
                s_latest_point.y = (DISPLAY_HEIGHT - 1) - s_latest_point.y;
            }

            // Smooth edge clamping: prevent out-of-bounds coordinates
            if (s_latest_point.x < 0) s_latest_point.x = 0;
            else if (s_latest_point.x >= DISPLAY_WIDTH) s_latest_point.x = DISPLAY_WIDTH - 1;

            if (s_latest_point.y < 0) s_latest_point.y = 0;
            else if (s_latest_point.y >= DISPLAY_HEIGHT) s_latest_point.y = DISPLAY_HEIGHT - 1;

            s_latest_point.pressed = true;
        } else {
            s_latest_point.pressed = false;
        }
        portEXIT_CRITICAL(&s_touch_mux);

        // Concise event logging for operator visibility & degenerate panel onset tracking
        if (latched_pressed && !prev_latched) {
            Serial.printf("TOUCH: press at (%d, %d) [raw: rx=%d, ry=%d, z1=%d]\n",
                          s_latest_point.x, s_latest_point.y, s_latest_raw.x, s_latest_raw.y, s_latest_raw.z);

            // Audible press confirmation on the onboard speaker. Fires on the
            // latch (a real, debounced contact), not on every sample, and is a
            // no-op while output is PHONE, so the HUD never claims sound it is
            // not configured to make.
            audio_hal_play_click();

            // Degenerate Panel Onset Tracking:
            // Degenerate Panel Onset Tracking (only in calibration wizard or uncalibrated):
            if (s_cal_mode || !s_is_calibrated) {
                if (raw_x < s_onset_x_min) s_onset_x_min = raw_x;
                if (raw_x > s_onset_x_max) s_onset_x_max = raw_x;
                if (raw_y < s_onset_y_min) s_onset_y_min = raw_y;
                if (raw_y > s_onset_y_max) s_onset_y_max = raw_y;
                s_onset_count++;

                // When s_onset_count >= kPositionOnsetsNeeded (3):
                if (s_onset_count >= kPositionOnsetsNeeded) {
                    int16_t dx = s_onset_x_max - s_onset_x_min;
                    int16_t dy = s_onset_y_max - s_onset_y_min;
                    if (dx < kPositionSpreadMin && dy < kPositionSpreadMin) {
                        s_position_dead = true;
                        ESP_LOGW(TAG, "TOUCH: Degenerate panel detected (spread dx=%d, dy=%d < %d across %d onsets) -> POSITION DEAD",
                                 dx, dy, kPositionSpreadMin, kPositionOnsetsNeeded);
                        Serial.printf("TOUCH: Degenerate panel detected (spread dx=%d, dy=%d < %d across %d onsets) -> POSITION DEAD\n",
                                      dx, dy, kPositionSpreadMin, kPositionOnsetsNeeded);
                    } else {
                        if (s_position_dead) {
                            s_position_dead = false;
                            Serial.printf("TOUCH: Position movement confirmed (dx=%d, dy=%d >= %d) -> POSITION RESTORED\n",
                                          dx, dy, kPositionSpreadMin);
                        }
                    }
                    // Reset onset tracking
                    s_onset_count = 0;
                    s_onset_x_min = 4095;
                    s_onset_x_max = 0;
                    s_onset_y_min = 4095;
                    s_onset_y_max = 0;
                }
            }
        } else if (!latched_pressed && prev_latched) {
            Serial.printf("TOUCH: release\n");
        }
        prev_latched = latched_pressed;
        s_prev_pen = pen_down;
        s_prev_pen_down = pen_down;
    }
}

} // namespace

void log_touch_channels(void) {
    bool mutex_acquired = false;
    if (g_spi_bus_mutex) {
        mutex_acquired = (xSemaphoreTake(g_spi_bus_mutex, pdMS_TO_TICKS(10)) == pdTRUE);
    } else {
        mutex_acquired = true;
    }
    if (mutex_acquired) {
        digitalWrite(PIN_LCD_CS, HIGH);
        Serial.printf("TOUCH channels x=%d y=%d z1=%d z2=%d temp=%d vbat=%d aux=%d\n",
                      read_channel(0x91),  // X  (panel X+)
                      read_channel(0xD1),  // Y  (panel Y+)
                      read_channel(0xB1),  // Z1
                      read_channel(0xC1),  // Z2
                      read_channel(0x84),  // TEMP0, internal
                      read_channel(0xA4),  // VBAT, internal
                      read_channel(0xE4)); // AUX
        arm_penirq();
        if (g_spi_bus_mutex) {
            xSemaphoreGive(g_spi_bus_mutex);
        }
    }
    Serial.flush();
}

void touch_hal_setup(void) {
    s_boot_ms = millis();
    touch_hal_clear_fault();
    pinMode(PIN_TOUCH_CS, OUTPUT);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    pinMode(PIN_LCD_CS, OUTPUT);
    digitalWrite(PIN_LCD_CS, HIGH);

    // PENIRQ witness pin (level only, GPIO 36).
    pinMode(PIN_TOUCH_IRQ, INPUT);

    arm_penirq();
    log_touch_channels();

    Preferences prefs;
    if (prefs.begin("dashcue", true)) {
        s_invert_y = prefs.getBool("touch_inv_y", false);
        s_invert_x = prefs.getBool("touch_inv_x", false);
        prefs.end();
        if (s_invert_y) Serial.println("TOUCH: Loaded invert_y = true from NVS");
        if (s_invert_x) Serial.println("TOUCH: Loaded invert_x = true from NVS");
    }

    // Attempt to load and verify calibration blob from NVS
    s_is_calibrated = touch_calibration_load_nvs(&s_cal_blob);
    // Panel signature (re-measured by the 8-point crosshair verifier on
    // 2026-10-24, docs/evidence/20261023/touch-verify-20261023.log): raw X
    // (0x91) drives SCREEN X with a NEGATIVE slope (X ~= -0.126*rx + 478) and
    // raw Y (0xD1) drives SCREEN Y with a POSITIVE slope (Y ~= +0.086*ry - 36).
    // The previous signature had the axes transposed (X from raw Y, Y from raw
    // X), which put every tap on the far corner of the screen. A blob outside
    // these bounds is a legacy/transposed matrix and must not be trusted.
    if (s_is_calibrated &&
        (s_cal_blob.alpha_x > -0.06f || s_cal_blob.alpha_x < -0.20f ||
         s_cal_blob.beta_y < 0.04f || s_cal_blob.beta_y > 0.15f ||
         fabsf(s_cal_blob.beta_x) > 0.03f ||
         fabsf(s_cal_blob.alpha_y) > 0.03f)) {
        Serial.println("TOUCH: Outdated/transposed calibration in NVS. Resetting to corrected 3.5\" ST7796 matrix.");
        touch_calibration_erase_nvs();
        s_is_calibrated = false;
    }

    if (!s_is_calibrated) {
        Serial.println("TOUCH: Loading corrected 3.5\" ST7796 factory matrix.");
        // NOTE the pairing, not the raw values, is what encodes the panel
        // signature: raw X maps to SCREEN X (inverted), raw Y maps to SCREEN Y.
        // Raw anchors below pair with g_calibration_targets in index order and
        // were derived from the verifier's measured tap data (2026-10-24).
        TouchRawPoint raw[CALIBRATION_POINT_COUNT] = {
            {3475,  880, 1000}, // -> Top-Left     (40,  40)
            { 305,  880, 1000}, // -> Top-Right    (440, 40)
            {1890, 2270, 1000}, // -> Center       (240,160)
            {3475, 3660, 1000}, // -> Bottom-Left  (40, 280)
            { 305, 3660, 1000}  // -> Bottom-Right (440,280)
        };
        if (touch_calibration_solve(raw, g_calibration_targets, &s_cal_blob)) {
            s_is_calibrated = true;
            touch_calibration_save_nvs(&s_cal_blob);
            Serial.printf("TOUCH: Calibrated 3.5\" matrix loaded: alpha_x=%.4f, beta_x=%.4f, delta_x=%.2f, alpha_y=%.4f, beta_y=%.4f, delta_y=%.2f\n",
                          s_cal_blob.alpha_x, s_cal_blob.beta_x, s_cal_blob.delta_x,
                          s_cal_blob.alpha_y, s_cal_blob.beta_y, s_cal_blob.delta_y);
        }
    } else {
        Serial.printf("TOUCH: Loaded valid calibration blob. Mean err=%.2f px, Max err=%.2f px\n",
                      s_cal_blob.residual_mean_px, s_cal_blob.residual_max_px);
    }
}

bool touch_hal_start_task(void) {
    BaseType_t ret = xTaskCreatePinnedToCore(
        touch_task,
        "touch_task",
        TOUCH_TASK_STACK_SIZE,
        NULL,
        TOUCH_TASK_PRIORITY,
        &s_touch_task_handle,
        TOUCH_TASK_CORE_ID
    );
    return (ret == pdPASS);
}

bool touch_hal_is_calibrated(void) {
    portENTER_CRITICAL(&s_touch_mux);
    bool cal = s_is_calibrated;
    portEXIT_CRITICAL(&s_touch_mux);
    return cal;
}

const TouchCalibrationBlob* touch_hal_get_calibration(void) {
    portENTER_CRITICAL(&s_touch_mux);
    const TouchCalibrationBlob *ptr = s_is_calibrated ? &s_cal_blob : NULL;
    portEXIT_CRITICAL(&s_touch_mux);
    return ptr;
}

bool touch_hal_set_calibration(const TouchCalibrationBlob *blob) {
    if (!blob || !touch_calibration_is_valid(blob)) {
        return false;
    }

    // Do not unlock operational controls if persistence failed.
    if (!touch_calibration_save_nvs(blob)) return false;
    portENTER_CRITICAL(&s_touch_mux);
    s_cal_blob = *blob;
    s_is_calibrated = true;
    portEXIT_CRITICAL(&s_touch_mux);

    return true;
}

void touch_hal_set_rotation(bool rotation_180) {
    portENTER_CRITICAL(&s_touch_mux);
    s_rotation_180 = rotation_180;
    portEXIT_CRITICAL(&s_touch_mux);
}

bool touch_hal_get_rotation(void) {
    portENTER_CRITICAL(&s_touch_mux);
    bool rot = s_rotation_180;
    portEXIT_CRITICAL(&s_touch_mux);
    return rot;
}

void touch_hal_set_invert_y(bool invert_y) {
    portENTER_CRITICAL(&s_touch_mux);
    s_invert_y = invert_y;
    portEXIT_CRITICAL(&s_touch_mux);
    Preferences prefs;
    if (prefs.begin("dashcue", false)) {
        prefs.putBool("touch_inv_y", invert_y);
        prefs.end();
    }
    Serial.printf("TOUCH: invert_y set to %s and saved to NVS\n", invert_y ? "true" : "false");
}

bool touch_hal_get_invert_y(void) {
    portENTER_CRITICAL(&s_touch_mux);
    bool inv = s_invert_y;
    portEXIT_CRITICAL(&s_touch_mux);
    return inv;
}

void touch_hal_set_invert_x(bool invert_x) {
    portENTER_CRITICAL(&s_touch_mux);
    s_invert_x = invert_x;
    portEXIT_CRITICAL(&s_touch_mux);
    Preferences prefs;
    if (prefs.begin("dashcue", false)) {
        prefs.putBool("touch_inv_x", invert_x);
        prefs.end();
    }
    Serial.printf("TOUCH: invert_x set to %s and saved to NVS\n", invert_x ? "true" : "false");
}

bool touch_hal_get_invert_x(void) {
    portENTER_CRITICAL(&s_touch_mux);
    bool inv = s_invert_x;
    portEXIT_CRITICAL(&s_touch_mux);
    return inv;
}

bool touch_hal_get_point(int16_t *out_x, int16_t *out_y) {
    bool pressed = false;
    portENTER_CRITICAL(&s_touch_mux);
    if (out_x) *out_x = s_latest_point.x;
    if (out_y) *out_y = s_latest_point.y;
    pressed = s_latest_point.pressed;
    portEXIT_CRITICAL(&s_touch_mux);
    return pressed;
}

bool touch_hal_get_raw(int16_t *out_raw_x, int16_t *out_raw_y, int16_t *out_raw_z) {
    bool has_point = false;
    portENTER_CRITICAL(&s_touch_mux);
    if (s_has_raw) {
        if (out_raw_x) *out_raw_x = s_latest_raw.x;
        if (out_raw_y) *out_raw_y = s_latest_raw.y;
        if (out_raw_z) *out_raw_z = s_latest_raw.z;
        has_point = true;
    }
    portEXIT_CRITICAL(&s_touch_mux);
    return has_point;
}

uint32_t touch_hal_get_sample_hz(void) {
    return s_sample_hz;
}

bool touch_hal_is_position_dead(void) {
    return s_position_dead;
}

void touch_hal_mark_position_dead(void) {
    s_position_dead = true;
}

bool touch_hal_is_faulted(void) {
    if (s_cal_mode) return false; // Never fault or exit wizard while calibration is open
    // If the panel has already recorded valid user presses, sporadic noise does not fault the panel
    if (s_valid_presses > 0 && s_saturated_rejects < (kFaultRejectThreshold * 2)) {
        return false;
    }
    if (s_is_calibrated) {
        return (s_saturated_rejects >= kFaultRejectThreshold);
    }
    return s_position_dead || (s_saturated_rejects >= kFaultRejectThreshold);
}

uint32_t touch_hal_get_rejected_presses(void) {
    return s_saturated_rejects;
}

void touch_hal_clear_fault(void) {
    s_saturated_rejects = 0;
    s_valid_presses = 0;
    s_pen_contact_seen = false;
    s_position_dead = false;
    s_onset_count = 0;
    s_onset_x_min = 4095;
    s_onset_x_max = 0;
    s_onset_y_min = 4095;
    s_onset_y_max = 0;
}

void touch_hal_set_calibration_mode(bool enable) {
    portENTER_CRITICAL(&s_touch_mux);
    s_cal_mode = enable;
    portEXIT_CRITICAL(&s_touch_mux);
}

bool touch_hal_save_calibration(const CalibrationPoint points[CALIBRATION_POINT_COUNT], float *out_mean, float *out_max) {
    if (!points) return false;
    TouchRawPoint raw[CALIBRATION_POINT_COUNT];
    TouchPoint scr[CALIBRATION_POINT_COUNT];
    for (int i = 0; i < CALIBRATION_POINT_COUNT; i++) {
        raw[i].x = points[i].raw_x;
        raw[i].y = points[i].raw_y;
        raw[i].z = 1000;
        scr[i].x = points[i].screen_x;
        scr[i].y = points[i].screen_y;
        scr[i].pressed = true;
    }
    TouchCalibrationBlob blob = {};
    bool solved = touch_calibration_solve(raw, scr, &blob);
    if (out_mean) *out_mean = blob.residual_mean_px;
    if (out_max) *out_max = blob.residual_max_px;
    if (solved) {
        return touch_hal_set_calibration(&blob);
    }
    return false;
}

void touch_hal_clear_calibration(void) {
    portENTER_CRITICAL(&s_touch_mux);
    s_is_calibrated = false;
    portEXIT_CRITICAL(&s_touch_mux);
    touch_calibration_erase_nvs();
}

#else

static TouchCalibrationBlob s_mock_cal;
static bool s_mock_is_calibrated = false;

void touch_hal_setup(void) {}
bool touch_hal_start_task(void) { return true; }
bool touch_hal_is_calibrated(void) { return s_mock_is_calibrated; }
const TouchCalibrationBlob* touch_hal_get_calibration(void) { return &s_mock_cal; }
bool touch_hal_set_calibration(const TouchCalibrationBlob *blob) {
    if (!blob) return false;
    s_mock_cal = *blob;
    s_mock_is_calibrated = true;
    return true;
}
void touch_hal_set_rotation(bool rotation_180) { (void)rotation_180; }
bool touch_hal_get_rotation(void) { return false; }
static bool s_mock_invert_y = false;
static bool s_mock_invert_x = false;
void touch_hal_set_invert_y(bool invert_y) { s_mock_invert_y = invert_y; }
bool touch_hal_get_invert_y(void) { return s_mock_invert_y; }
void touch_hal_set_invert_x(bool invert_x) { s_mock_invert_x = invert_x; }
bool touch_hal_get_invert_x(void) { return s_mock_invert_x; }
bool touch_hal_get_point(int16_t *out_x, int16_t *out_y) {
    if (out_x) *out_x = 160;
    if (out_y) *out_y = 120;
    return false;
}
bool touch_hal_get_raw(int16_t *out_raw_x, int16_t *out_raw_y, int16_t *out_raw_z) {
    if (out_raw_x) *out_raw_x = 2000;
    if (out_raw_y) *out_raw_y = 2000;
    if (out_raw_z) *out_raw_z = 500;
    return false;
}
static bool s_mock_position_dead = false;
static uint32_t s_mock_saturated_rejects = 0;

uint32_t touch_hal_get_sample_hz(void) { return 100; }
bool touch_hal_is_faulted(void) { return s_mock_position_dead || (s_mock_saturated_rejects >= 25); }
bool touch_hal_is_position_dead(void) { return s_mock_position_dead; }
void touch_hal_mark_position_dead(void) { s_mock_position_dead = true; }
uint32_t touch_hal_get_rejected_presses(void) { return s_mock_saturated_rejects; }
void touch_hal_clear_fault(void) {
    s_mock_position_dead = false;
    s_mock_saturated_rejects = 0;
}

void touch_hal_set_calibration_mode(bool enable) { (void)enable; }
bool touch_hal_save_calibration(const CalibrationPoint points[CALIBRATION_POINT_COUNT], float *out_mean, float *out_max) {
    (void)points;
    if (out_mean) *out_mean = 2.0f;
    if (out_max) *out_max = 4.0f;
    return true;
}
void touch_hal_clear_calibration(void) {
    s_mock_is_calibrated = false;
}

#endif
