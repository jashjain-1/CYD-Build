// DashCue LVGL assert handler implementation.
//
// Compiled for both the product and the host (native) test environment so the
// declaration in include/dashcue_lvgl_assert.h always resolves.
#include "dashcue_lvgl_assert.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <Arduino.h>
#include <esp_system.h>
#else
#include <stdio.h>
#include <stdlib.h>
#endif

extern "C" void dashcue_lvgl_assert_failed(const char *file, int line) {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    Serial.printf("LVGL ASSERT FAILED at %s:%d - restarting (crash counter++)\n", file, line);
    Serial.flush();
    esp_restart();
#else
    fprintf(stderr, "LVGL ASSERT FAILED at %s:%d - aborting\n", file, line);
    fflush(stderr);
    abort();
#endif
    // Unreachable: both branches terminate the program. Kept so the compiler sees
    // a complete function and callers can rely on "never returns".
    for (;;) {
    }
}
