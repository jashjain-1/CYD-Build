#include "board_hal.h"
#include "dashcue_config.h"

#include <stdio.h>
#include <string.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <Arduino.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_heap_caps.h>

static uint8_t s_backlight_pct = 80;

void board_init(void) {
    // Configure RGB LED pins (CYD RGB LED is active-LOW, so HIGH turns OFF)
    pinMode(PIN_LED_R, OUTPUT);
    pinMode(PIN_LED_G, OUTPUT);
    pinMode(PIN_LED_B, OUTPUT);
    digitalWrite(PIN_LED_R, HIGH);
    digitalWrite(PIN_LED_G, HIGH);
    digitalWrite(PIN_LED_B, HIGH);

    // Ambient light sensor input
    pinMode(PIN_LDR, INPUT);

    // Configure Backlight PWM using LEDC
    ledcSetup(BL_PWM_CHANNEL, BL_PWM_FREQ_HZ, BL_PWM_RESOLUTION_BITS);
    ledcAttachPin(PIN_LCD_BL, BL_PWM_CHANNEL);
    board_set_backlight_pct(s_backlight_pct);
}

void board_set_backlight_pct(uint8_t pct) {
    if (pct < 10) pct = 10;
    if (pct > 100) pct = 100;
    s_backlight_pct = pct;

    // 8-bit duty: 0 to 255. 10% is 26, 100% is 255
    uint32_t duty = (uint32_t)((pct * 255) / 100);
    ledcWrite(BL_PWM_CHANNEL, duty);
}

uint8_t board_get_backlight_pct(void) {
    return s_backlight_pct;
}

uint16_t board_read_ambient_light(void) {
    return (uint16_t)analogRead(PIN_LDR);
}

void board_get_mac(uint8_t mac[6]) {
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        memset(mac, 0, 6);
    }
}

void board_get_device_id(char *out_id, size_t max_len) {
    uint8_t mac[6] = {0};
    board_get_mac(mac);
    snprintf(out_id, max_len, "DASHCUE-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

esp_reset_reason_t board_get_reset_reason(void) {
    return esp_reset_reason();
}

const char* board_get_reset_reason_str(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "Power-On";
        case ESP_RST_EXT:       return "External Pin";
        case ESP_RST_SW:        return "Software Reset";
        case ESP_RST_PANIC:     return "Exception / Panic";
        case ESP_RST_INT_WDT:   return "Interrupt WDT";
        case ESP_RST_TASK_WDT:  return "Task WDT";
        case ESP_RST_WDT:       return "Other WDT";
        case ESP_RST_DEEPSLEEP: return "Deep Sleep Wakeup";
        case ESP_RST_BROWNOUT:  return "Brownout";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "Unknown";
    }
}

uint32_t board_get_free_heap(void) {
    return (uint32_t)ESP.getFreeHeap();
}

uint32_t board_get_min_free_heap(void) {
    return (uint32_t)ESP.getMinFreeHeap();
}

uint32_t board_get_largest_free_block(void) {
    return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

void board_set_led_rgb(bool r, bool g, bool b) {
    // CYD RGB LEDs are common anode (active LOW)
    digitalWrite(PIN_LED_R, r ? LOW : HIGH);
    digitalWrite(PIN_LED_G, g ? LOW : HIGH);
    digitalWrite(PIN_LED_B, b ? LOW : HIGH);
}

void board_reboot(void) {
    esp_restart();
}

uint8_t board_get_backlight_brightness(void) {
    return board_get_backlight_pct();
}

void board_set_backlight_brightness(uint8_t pct) {
    board_set_backlight_pct(pct);
}

#include "state/system_state.h"

bool board_is_safemode_triggered(void) {
    return system_state_is_safemode();
}

uint32_t board_get_reset_count(void) {
    return system_state_get_reset_count();
}

uint32_t board_get_last_reset_reason(void) {
    return (uint32_t)system_state_get_last_reset_reason();
}

void board_reset_crash_tracker(void) {
    system_state_clear_safemode();
}

#else

static uint8_t s_mock_backlight = 80;

void board_init(void) {}
void board_set_backlight_pct(uint8_t pct) {
    if (pct < 10) pct = 10;
    if (pct > 100) pct = 100;
    s_mock_backlight = pct;
}
uint8_t board_get_backlight_pct(void) { return s_mock_backlight; }
uint16_t board_read_ambient_light(void) { return 2048; }
void board_get_mac(uint8_t mac[6]) {
    mac[0] = 0xD4; mac[1] = 0xE9; mac[2] = 0xF4;
    mac[3] = 0x6F; mac[4] = 0xF0; mac[5] = 0x88;
}
void board_get_device_id(char *out_id, size_t max_len) {
    snprintf(out_id, max_len, "DASHCUE-D4E9F46FF088");
}
esp_reset_reason_t board_get_reset_reason(void) { return ESP_RST_POWERON; }
const char* board_get_reset_reason_str(esp_reset_reason_t reason) { (void)reason; return "Power-On"; }
uint32_t board_get_free_heap(void) { return 161000; }
uint32_t board_get_min_free_heap(void) { return 140000; }
uint32_t board_get_largest_free_block(void) { return 110000; }
void board_set_led_rgb(bool r, bool g, bool b) { (void)r; (void)g; (void)b; }
void board_reboot(void) {}

uint8_t board_get_backlight_brightness(void) { return s_mock_backlight; }
void board_set_backlight_brightness(uint8_t pct) { board_set_backlight_pct(pct); }
bool board_is_safemode_triggered(void) { return false; }
uint32_t board_get_reset_count(void) { return 1; }
uint32_t board_get_last_reset_reason(void) { return 1; }
void board_reset_crash_tracker(void) {}

#endif
