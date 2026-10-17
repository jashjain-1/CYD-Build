#include "touch_hal.h"
#include "dashcue_config.h"

#include <string.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

namespace {

SPIClass s_touch_bus(HSPI);
XPT2046_Touchscreen s_touch(PIN_TOUCH_CS);

TouchCalibrationBlob s_cal_blob;
bool s_is_calibrated = false;
bool s_rotation_180 = false;
bool s_cal_mode = false;

portMUX_TYPE s_touch_mux = portMUX_INITIALIZER_UNLOCKED;

TouchPoint s_latest_point = {0, 0, false};
TouchRawPoint s_latest_raw = {0, 0, 0};
bool s_has_raw = false;

volatile uint32_t s_sample_counter = 0;
volatile uint32_t s_sample_hz = 0;
uint32_t s_last_hz_calc_ms = 0;

TaskHandle_t s_touch_task_handle = NULL;

void touch_task(void *param) {
    (void)param;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(10); // 10 ms = 100 Hz

    while (1) {
        vTaskDelayUntil(&last_wake_time, period_ticks);

        uint32_t now = millis();
        s_sample_counter++;
        if (now - s_last_hz_calc_ms >= 1000) {
            s_sample_hz = s_sample_counter;
            s_sample_counter = 0;
            s_last_hz_calc_ms = now;
        }

        bool touched = s_touch.touched();
        if (touched) {
            TS_Point p = s_touch.getPoint();

            // Saturated read rejection (KI-04 / docs/HARDWARE_REPORT.md):
            // Floating or unasserted MISO returns 0xFFFF -> x=y=8191, z=4095.
            // Reject any read where z >= 4090 or x,y outside [0..4095].
            bool plausible = (p.z < 4090) && (p.x >= 0 && p.x <= 4095) && (p.y >= 0 && p.y <= 4095);

            portENTER_CRITICAL(&s_touch_mux);
            if (plausible) {
                s_latest_raw.x = p.x;
                s_latest_raw.y = p.y;
                s_latest_raw.z = p.z;
                s_has_raw = true;

                if (s_cal_mode) {
                    // In calibration mode, CalibrationScreen consumes raw points via touch_hal_get_raw.
                    // Suppress LVGL pointer indev clicks so UI buttons aren't triggered.
                    s_latest_point.pressed = false;
                } else if (s_is_calibrated) {
                    s_latest_point = touch_calibration_apply(&s_cal_blob, p.x, p.y, s_rotation_180);
                } else {
                    // Unverified coordinates must never activate operational
                    // buttons. The wizard consumes raw samples independently.
                    s_latest_point.pressed = false;
                }
            } else {
                s_latest_point.pressed = false;
                s_has_raw = false;
            }
            portEXIT_CRITICAL(&s_touch_mux);

            static uint32_t s_last_touch_log_ms = 0;
            if (plausible && (now - s_last_touch_log_ms >= 300)) {
                s_last_touch_log_ms = now;
                Serial.printf("TOUCH raw=(%d,%d,z=%d) scr=(%d,%d)\n", (int)p.x, (int)p.y, (int)p.z, (int)s_latest_point.x, (int)s_latest_point.y);
            }
        } else {
            portENTER_CRITICAL(&s_touch_mux);
            s_latest_point.pressed = false;
            s_has_raw = false;
            portEXIT_CRITICAL(&s_touch_mux);
        }
    }
}

} // namespace

void touch_hal_setup(void) {
    pinMode(PIN_TOUCH_CS, OUTPUT);
    digitalWrite(PIN_TOUCH_CS, HIGH);

    // Start dedicated HSPI bus for touch with explicit pins (25/39/32/33)
    s_touch_bus.begin(PIN_TOUCH_SCLK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, PIN_TOUCH_CS);
    // XPT2046::begin calls SPIClass::begin() with default pins. Initialize our
    // dedicated bus first: a second begin does not replace an active ESP32 bus.
    s_touch.begin(s_touch_bus);
    s_touch.setRotation(1);

    // Attempt to load and verify calibration blob from NVS
    s_is_calibrated = touch_calibration_load_nvs(&s_cal_blob);
    if (!s_is_calibrated) {
        Serial.println("TOUCH: No valid measured calibration in NVS. Complete the five touch targets.");
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
    TouchCalibrationBlob blob;
    memset(&blob, 0, sizeof(blob));
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
uint32_t touch_hal_get_sample_hz(void) { return 100; }

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
