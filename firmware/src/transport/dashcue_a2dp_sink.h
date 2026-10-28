#pragma once

#if defined(DASHCUE_ENABLE_A2DP)

#include <Arduino.h>
#include <BluetoothA2DPSink.h>
#include <esp_gap_bt_api.h>
#include <esp_gap_ble_api.h>
#include "state/app_state.h"
#include "audio/audio_hal.h"

class DashCueA2DPSink : public BluetoothA2DPSink {
public:
    DashCueA2DPSink() : BluetoothA2DPSink() {}

    // Speaker clarity filter: 120 Hz 2nd-order high-pass (Q = 0.7071, fs =
    // 44100) in transposed direct form II, applied in place before the base
    // class writes to I2S. The 8 Ω 0.5 W driver cannot reproduce sub-bass;
    // feeding it anyway wastes excursion and mids/m highs get masked by cone
    // distortion. DC gain is exactly 0 and passband gain 1.0, so level is
    // untouched above ~200 Hz. Coefficients normalised a0 = 1.0120888:
    //   b0 = 0.987984, b1 = -1.975970, b2 = 0.987984
    //   a1 = -1.975820, a2 = 0.976111
    // Bounded arithmetic only: no allocation, no locks - safe in the audio
    // callback (the base does equally sized swap/volume loops here).
    void audio_data_callback(const uint8_t *data, uint32_t len) override {
        // Concurrency Guard: If an alert tone is actively playing on Core 1 audio_task,
        // suppress A2DP frames to prevent concurrent I2S DMA bus collision, sample rate
        // clashing (16 kHz vs 44.1 kHz), and Bluedroid task deadlock/panics.
        if (audio_hal_is_playing()) {
            return;
        }

        if (len >= 4 && data != nullptr) {
            int16_t *pcm = const_cast<int16_t *>(reinterpret_cast<const int16_t *>(data));
            const uint32_t frames = len / 4;   // stereo Frame = 2 x int16
            for (uint32_t i = 0; i < frames; ++i) {
                pcm[i * 2]     = hp_step(m_hp_l, pcm[i * 2]);
                pcm[i * 2 + 1] = hp_step(m_hp_r, pcm[i * 2 + 1]);
            }
        }
        BluetoothA2DPSink::audio_data_callback(data, len);
    }

    // Expose protected methods from BluetoothA2DPCommon for setup & peer reconnection
    void set_last_connection(esp_bd_addr_t bda) override {
        BluetoothA2DPCommon::set_last_connection(bda);
    }

    bool has_last_connection() override {
        return BluetoothA2DPCommon::has_last_connection();
    }

    // Override clean_last_connection to prevent timeout-induced wiping of NVS "last_bda"
    void clean_last_connection() override {
        Serial.println("A2DP: clean_last_connection intercepted - preserving persistent bond across reboot");
    }

    // Explicitly purge bond only when commanded by user / forget_phone
    void force_clean_last_connection() {
        BluetoothA2DPCommon::clean_last_connection();
    }

    void reset_filter() {
        m_hp_l.s1 = 0.0f;
        m_hp_l.s2 = 0.0f;
        m_hp_r.s1 = 0.0f;
        m_hp_r.s2 = 0.0f;
    }

private:
    struct HpCh {
        float s1 = 0.0f;
        float s2 = 0.0f;
    };
    HpCh m_hp_l;
    HpCh m_hp_r;

    static int16_t hp_step(HpCh &c, float x) {
        const float y = 0.987984f * x + c.s1;
        float clamped_y = y;
        if (clamped_y > 32767.0f) clamped_y = 32767.0f;
        else if (clamped_y < -32768.0f) clamped_y = -32768.0f;
        c.s1 = -1.975970f * x + 1.975820f * clamped_y + c.s2;
        c.s2 = 0.987984f * x - 0.976111f * clamped_y;
        return (int16_t)clamped_y;
    }

    // Intercept Classic BT GAP events to keep app_state, BLE whitelist, and NVS in sync
    void app_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) override {
        if (event == ESP_BT_GAP_AUTH_CMPL_EVT && param != nullptr) {
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                Serial.printf("A2DP: Classic BT paired successfully: %02X:%02X:%02X:%02X:%02X:%02X\n",
                              param->auth_cmpl.bda[0], param->auth_cmpl.bda[1], param->auth_cmpl.bda[2],
                              param->auth_cmpl.bda[3], param->auth_cmpl.bda[4], param->auth_cmpl.bda[5]);
                if (app_state_is_valid_bt_addr(param->auth_cmpl.bda)) {
                    // Synchronize app_state
                    app_state_set_bonded(true, param->auth_cmpl.bda);
                    // Synchronize BLE accept-list
                    esp_ble_gap_update_whitelist(true, param->auth_cmpl.bda, BLE_WL_ADDR_TYPE_PUBLIC);
                    // Persist to NVS
                    set_last_connection(param->auth_cmpl.bda);
                } else {
                    Serial.println("A2DP: auth complete with structurally invalid address - not bonded");
                }
            }
        }
        BluetoothA2DPSink::app_gap_callback(event, param);
    }
};

#endif // DASHCUE_ENABLE_A2DP
