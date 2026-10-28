#include "audio_hal.h"

#include <math.h>
#include <string.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/dac.h>
#include <driver/gpio.h>
#include <driver/i2s.h>

namespace {

constexpr int kSampleRateHz = 16000;

// Alert waveform: 4 pulses of 50 ms with 30 ms gaps (matches the phone's
// X-band chirp so both outputs sound like the same instrument).
constexpr int kPulseSamples = kSampleRateHz * 50 / 1000;   // 800
constexpr int kGapSamples = kSampleRateHz * 30 / 1000;     // 480
constexpr int kAlertSamples = 4 * kPulseSamples + 3 * kGapSamples; // 4640
constexpr int kRampSamples = 40;                            // ~2.5 ms Tukey taper
constexpr int kBeepPulse = 640;                            // 40 ms confirmation pulse
constexpr int kBeepGap = 320;                              // 20 ms gap
constexpr int kBeepSamples = 2 * kBeepPulse + kBeepGap;    // 1600
constexpr int kClickSamples = 160;                         // 10 ms tactile click
constexpr int kRampLen = 160;                              // 10 ms raised-cosine transition

// Precomputed 10 ms raised-cosine S-curve: round(128.0 * 0.5 * (1.0 - cos(pi * i / 159.0)))
// Placed in Flash .rodata (0 bytes static DRAM)
static const uint8_t kRampTable[kRampLen] = {
    0,   0,   0,   0,   0,   0,   0,   1,   1,   1,
    1,   2,   2,   2,   2,   3,   3,   4,   4,   4,
    5,   5,   6,   6,   7,   8,   8,   9,  10,  10,
   11,  12,  12,  13,  14,  15,  16,  16,  17,  18,
   19,  20,  21,  22,  23,  24,  25,  26,  27,  28,
   29,  30,  31,  32,  33,  34,  35,  36,  38,  39,
   40,  41,  42,  44,  45,  46,  47,  48,  50,  51,
   52,  53,  55,  56,  57,  58,  60,  61,  62,  63,
   65,  66,  67,  68,  70,  71,  72,  73,  75,  76,
   77,  78,  80,  81,  82,  83,  84,  86,  87,  88,
   89,  90,  92,  93,  94,  95,  96,  97,  98,  99,
  100, 101, 102, 103, 104, 105, 106, 107, 108, 109,
  110, 111, 112, 112, 113, 114, 115, 116, 116, 117,
  118, 118, 119, 120, 120, 121, 122, 122, 123, 123,
  124, 124, 124, 125, 125, 126, 126, 126, 126, 127,
  127, 127, 127, 128, 128, 128, 128, 128, 128, 128
};

enum AudioToneType : uint8_t {
    TONE_NONE = 0,
    TONE_ALERT,
    TONE_CLICK,
    TONE_BEEP
};

struct AudioCmd {
    AudioToneType tone;
};

// Queue and task handles for FreeRTOS Core 1 audio_task
static QueueHandle_t s_audio_cmd_queue = nullptr;
static TaskHandle_t s_audio_task_handle = nullptr;

static volatile bool s_playing = false;
static audio_duck_cb_t s_duck_cb = nullptr;
static volatile bool s_a2dp_streaming = false;

static audio_out_t s_output = AUDIO_OUT_CYD;
static uint32_t s_volume_pct = 80;
static int8_t s_eq_db[3] = {0, 0, 0};

static Preferences s_prefs;
static bool s_prefs_ready = false;

// ---------------------------------------------------------------------------
// 3-band streaming tone control (RBJ cookbook): low shelf, peaking, high shelf.
// Implemented as single-sample streaming biquads for 0-heap on-the-fly execution.
// ---------------------------------------------------------------------------
struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

Biquad lowShelf(float f0, float gain_db, float q) {
    const float A = powf(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * (float)M_PI * f0 / (float)kSampleRateHz;
    const float c = cosf(w0), s = sinf(w0);
    const float alpha = s / (2.0f * q);
    const float tsa = 2.0f * sqrtf(A) * alpha;
    const float a0 = A + 1.0f + (A - 1.0f) * c + tsa;
    Biquad b;
    b.b0 = (A * ((A + 1.0f) - (A - 1.0f) * c + tsa)) / a0;
    b.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * c)) / a0;
    b.b2 = (A * ((A + 1.0f) - (A - 1.0f) * c - tsa)) / a0;
    b.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * c)) / a0;
    b.a2 = ((A + 1.0f) + (A - 1.0f) * c - tsa) / a0;
    return b;
}

Biquad peaking(float f0, float gain_db, float q) {
    const float A = powf(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * (float)M_PI * f0 / (float)kSampleRateHz;
    const float c = cosf(w0), s = sinf(w0);
    const float alpha = s / (2.0f * q);
    const float a0 = 1.0f + alpha / A;
    Biquad b;
    b.b0 = (1.0f + alpha * A) / a0;
    b.b1 = (-2.0f * c) / a0;
    b.b2 = (1.0f - alpha * A) / a0;
    b.a1 = (-2.0f * c) / a0;
    b.a2 = (1.0f - alpha / A) / a0;
    return b;
}

Biquad highShelf(float f0, float gain_db, float q) {
    const float A = powf(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * (float)M_PI * f0 / (float)kSampleRateHz;
    const float c = cosf(w0), s = sinf(w0);
    const float alpha = s / (2.0f * q);
    const float tsa = 2.0f * sqrtf(A) * alpha;
    const float a0 = A + 1.0f - (A - 1.0f) * c + tsa;
    Biquad b;
    b.b0 = (A * ((A + 1.0f) + (A - 1.0f) * c + tsa)) / a0;
    b.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * c)) / a0;
    b.b2 = (A * ((A + 1.0f) - (A - 1.0f) * c - tsa)) / a0;
    b.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * c)) / a0;
    b.a2 = ((A + 1.0f) - (A - 1.0f) * c - tsa) / a0;
    return b;
}

struct BiquadState {
    Biquad coeff;
    float x1 = 0.0f;
    float x2 = 0.0f;
    float y1 = 0.0f;
    float y2 = 0.0f;
    bool active = false;

    inline float process(float in) {
        if (!active) return in;
        float out = coeff.b0 * in + coeff.b1 * x1 + coeff.b2 * x2 - coeff.a1 * y1 - coeff.a2 * y2;
        x2 = x1;
        x1 = in;
        y2 = y1;
        y1 = out;
        return out;
    }
};

struct StreamingEq {
    BiquadState filters[3];
    float pre_scale = 1.0f;
    bool active = false;
};

void initStreamingEq(StreamingEq &eq, const int8_t eq_db[3]) {
    eq.active = (eq_db[0] != 0 || eq_db[1] != 0 || eq_db[2] != 0);
    if (!eq.active) return;

    int8_t max_boost = 0;
    for (int i = 0; i < 3; ++i) {
        if (eq_db[i] > max_boost) max_boost = eq_db[i];
    }
    eq.pre_scale = (max_boost > 0) ? powf(10.0f, -((float)max_boost) / 20.0f) : 1.0f;

    if (eq_db[0] != 0) {
        eq.filters[0].coeff = lowShelf(60.0f, (float)eq_db[0], 0.707f);
        eq.filters[0].active = true;
    } else {
        eq.filters[0].active = false;
    }
    eq.filters[0].x1 = eq.filters[0].x2 = eq.filters[0].y1 = eq.filters[0].y2 = 0.0f;

    if (eq_db[1] != 0) {
        eq.filters[1].coeff = peaking(1000.0f, (float)eq_db[1], 1.0f);
        eq.filters[1].active = true;
    } else {
        eq.filters[1].active = false;
    }
    eq.filters[1].x1 = eq.filters[1].x2 = eq.filters[1].y1 = eq.filters[1].y2 = 0.0f;

    if (eq_db[2] != 0) {
        eq.filters[2].coeff = highShelf(6000.0f, (float)eq_db[2], 0.707f);
        eq.filters[2].active = true;
    } else {
        eq.filters[2].active = false;
    }
    eq.filters[2].x1 = eq.filters[2].x2 = eq.filters[2].y1 = eq.filters[2].y2 = 0.0f;
}

inline float processStreamingEq(StreamingEq &eq, float sample) {
    if (!eq.active) return sample;
    float s = sample * eq.pre_scale;
    s = eq.filters[0].process(s);
    s = eq.filters[1].process(s);
    s = eq.filters[2].process(s);
    if (s > 1.0f) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    return s;
}

// ---------------------------------------------------------------------------
// On-the-fly sample evaluation functions
// Returns normalized waveform amplitude in range [-1.0, 1.0]
// ---------------------------------------------------------------------------
int get_tone_samples(AudioToneType tone) {
    switch (tone) {
        case TONE_ALERT: return kAlertSamples;
        case TONE_CLICK: return kClickSamples;
        case TONE_BEEP:  return kBeepSamples;
        default:         return 0;
    }
}

float evaluate_sample(AudioToneType tone, int n) {
    if (tone == TONE_CLICK) {
        if (n < 0 || n >= kClickSamples) return 0.0f;
        double env;
        if (n < 16) {
            env = 0.5 * (1.0 - cos(M_PI * (double)n / 16.0));
        } else {
            env = 0.5 * (1.0 + cos(M_PI * (double)(n - 16) / 143.0));
        }
        double val = sin(2.0 * M_PI * 1800.0 * (double)n / (double)kSampleRateHz) * env * 0.6;
        return (float)val;
    }

    if (tone == TONE_BEEP) {
        if (n < 0 || n >= kBeepSamples) return 0.0f;
        int pulse_n = -1;
        if (n < kBeepPulse) {
            pulse_n = n;
        } else if (n >= kBeepPulse + kBeepGap && n < kBeepSamples) {
            pulse_n = n - (kBeepPulse + kBeepGap);
        }
        if (pulse_n < 0) return 0.0f; // Gap between beeps

        constexpr int kRamp = 80; // 5 ms raised-cosine ramp
        double env = 1.0;
        if (pulse_n < kRamp) {
            env = 0.5 * (1.0 - cos(M_PI * (double)pulse_n / (double)kRamp));
        } else if (pulse_n > kBeepPulse - 1 - kRamp) {
            const int decay = kBeepPulse - 1 - pulse_n;
            env = 0.5 * (1.0 - cos(M_PI * (double)decay / (double)kRamp));
        }
        double val = sin(2.0 * M_PI * 1200.0 * (double)pulse_n / (double)kSampleRateHz) * env * 0.7;
        return (float)val;
    }

    if (tone == TONE_ALERT) {
        if (n < 0 || n >= kAlertSamples) return 0.0f;
        constexpr int kCycle = kPulseSamples + kGapSamples; // 1280
        const int pulse_idx = n / kCycle;
        const int offset = n % kCycle;
        if (pulse_idx >= 4 || offset >= kPulseSamples) {
            return 0.0f; // Gap between pulses
        }
        const int pulse_n = offset;
        const double t = (double)pulse_n / (double)kSampleRateHz;
        const double f0 = 1150.0;
        const double f1 = 2300.0;
        const double beta = (f1 - f0) / 0.050; // 23000 Hz/s
        const double phi = 2.0 * M_PI * (f0 * t + 0.5 * beta * t * t);
        const double w = 0.80 * sin(phi) + 0.20 * sin(2.0 * phi);

        double window = 1.0;
        if (pulse_n < kRampSamples) {
            window = 0.5 * (1.0 - cos(M_PI * (double)pulse_n / (double)kRampSamples));
        } else if (pulse_n > kPulseSamples - 1 - kRampSamples) {
            const int decay = kPulseSamples - 1 - pulse_n;
            window = 0.5 * (1.0 - cos(M_PI * (double)decay / (double)kRampSamples));
        }
        double val = w * window * 0.95;
        return (float)val;
    }

    return 0.0f;
}

void savePrefs(void) {
    if (!s_prefs_ready) return;
    s_prefs.putUInt("mode", (uint32_t)s_output);
    s_prefs.putUInt("vol", s_volume_pct);
    s_prefs.putChar("bass", (char)s_eq_db[0]);
    s_prefs.putChar("mid", (char)s_eq_db[1]);
    s_prefs.putChar("tre", (char)s_eq_db[2]);
}

// ---------------------------------------------------------------------------
// Hardware I2S DMA streaming engine
// Synthesizes on-the-fly into fixed 64-frame (256-byte) stack chunks.
// 0 dynamic heap allocations! Full clean 0..255 DAC dynamic range.
// ---------------------------------------------------------------------------
void stream_tone(AudioToneType tone) {
    const int total_samples = get_tone_samples(tone);
    if (total_samples <= 0) return;

    s_playing = true;

#if defined(DASHCUE_ENABLE_A2DP)
    const bool was_a2dp = s_a2dp_streaming;
    if (was_a2dp && s_duck_cb) {
        s_duck_cb(true);
    }
#endif

    // Setup I2S for alert synthesis at 16 kHz on built-in DAC
    i2s_set_sample_rates(I2S_NUM_0, kSampleRateHz);
    i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
    dac_output_enable(DAC_CHANNEL_2);

    StreamingEq eq;
    initStreamingEq(eq, s_eq_db);

    const int total_frames = total_samples + 2 * kRampLen;
    const uint32_t peak = (AUDIO_SAFE_PEAK_COUNTS * s_volume_pct) / AUDIO_MAX_VOLUME_PCT;

    constexpr size_t kChunkFrames = 64;
    uint16_t chunk_buf[kChunkFrames * 2]; // 256 bytes on task stack

    int frame_idx = 0;
    while (frame_idx < total_frames && (s_output == AUDIO_OUT_CYD)) {
        size_t chunk_len = (size_t)(total_frames - frame_idx);
        if (chunk_len > kChunkFrames) chunk_len = kChunkFrames;

        for (size_t i = 0; i < chunk_len; ++i) {
            int current_frame = frame_idx + (int)i;
            uint8_t dac_val = 128;

            if (current_frame < kRampLen) {
                // Phase 1: Macro lead-in S-curve (0 -> 128)
                dac_val = kRampTable[current_frame];
            } else if (current_frame < kRampLen + total_samples) {
                // Phase 2: Active audio PCM synthesis around midscale 128
                int sample_n = current_frame - kRampLen;
                float norm = evaluate_sample(tone, sample_n);
                if (eq.active) {
                    norm = processStreamingEq(eq, norm);
                }
                int32_t out = 128 + (int32_t)roundf(norm * (float)peak);
                if (out < 1) out = 1;
                if (out > 255) out = 255;
                dac_val = (uint8_t)out;
            } else {
                // Phase 3: Macro lead-out S-curve (128 -> 0)
                int down_idx = current_frame - (kRampLen + total_samples);
                if (down_idx < kRampLen) {
                    dac_val = kRampTable[kRampLen - 1 - down_idx];
                } else {
                    dac_val = 0;
                }
            }

            uint16_t sample_word = (uint16_t)dac_val << 8;
            chunk_buf[i * 2]     = sample_word; // Left (DAC1)
            chunk_buf[i * 2 + 1] = sample_word; // Right (DAC2 / GPIO 26)
        }

        size_t bytes_written = 0;
        i2s_write(I2S_NUM_0, chunk_buf, chunk_len * sizeof(uint32_t),
                  &bytes_written, portMAX_DELAY);
        frame_idx += (int)chunk_len;
    }

    // Flush/zero remaining DMA buffers
    i2s_zero_dma_buffer(I2S_NUM_0);

#if defined(DASHCUE_ENABLE_A2DP)
    if (s_a2dp_streaming) {
        // Return I2S to 44.1 kHz for A2DP music before releasing the s_playing concurrency gate
        i2s_set_sample_rates(I2S_NUM_0, 44100);
        i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
        s_playing = false;
        if (s_duck_cb) s_duck_cb(false);
    } else {
        s_playing = false;
        audio_hal_clamp_zero_volts();
    }
#else
    s_playing = false;
    audio_hal_clamp_zero_volts();
#endif
}

void audio_task_func(void *pvParameters) {
    (void)pvParameters;
    AudioCmd cmd;
    while (true) {
        if (xQueueReceive(s_audio_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (s_output == AUDIO_OUT_CYD && cmd.tone != TONE_NONE) {
                stream_tone(cmd.tone);
            }
        }
    }
}

} // namespace

void audio_hal_set_duck_callback(audio_duck_cb_t cb) {
    s_duck_cb = cb;
}

void audio_hal_clamp_zero_volts(void) {
    i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE);
    dac_output_voltage(DAC_CHANNEL_2, 0);
    dac_output_disable(DAC_CHANNEL_2);
    gpio_set_direction(GPIO_NUM_26, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_26, 0);
}

void audio_hal_idle_state(void) {
    audio_hal_clamp_zero_volts();
}

void audio_hal_notify_a2dp_state(bool is_streaming) {
    s_a2dp_streaming = is_streaming;
    if (!is_streaming && !audio_hal_is_playing()) {
        audio_hal_clamp_zero_volts();
    }
}

bool audio_hal_is_a2dp_streaming(void) {
    return s_a2dp_streaming;
}

void audio_hal_init(void) {
    // 1. Ensure I2S driver is installed on I2S_NUM_0 in built-in DAC mode
    i2s_config_t i2s_config = {};
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
    i2s_config.sample_rate = 16000;
    i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
    i2s_config.intr_alloc_flags = 0;
    i2s_config.dma_buf_count = 4;
    i2s_config.dma_buf_len = 64;
    i2s_config.use_apll = false;
    i2s_config.tx_desc_auto_clear = true;
    i2s_config.fixed_mclk = 0;
    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (err == ESP_OK) {
        i2s_set_pin(I2S_NUM_0, NULL);
    }

    // 2. Ensure DAC peripheral is disabled and GPIO 26 is clamped to digital 0V at boot
    audio_hal_clamp_zero_volts();

    // 3. Load preferences from NVS
    s_prefs.begin("dashcue_audio", false);
    s_prefs_ready = true;
    s_output = (audio_out_t)s_prefs.getUInt("mode", (uint32_t)AUDIO_OUT_CYD);
    s_volume_pct = s_prefs.getUInt("vol", 80);
    if (s_volume_pct > AUDIO_MAX_VOLUME_PCT) s_volume_pct = AUDIO_MAX_VOLUME_PCT;
    s_eq_db[0] = (int8_t)s_prefs.getChar("bass", 0);
    s_eq_db[1] = (int8_t)s_prefs.getChar("mid", 0);
    s_eq_db[2] = (int8_t)s_prefs.getChar("tre", 0);

    // 4. Create FreeRTOS command queue and spawn dedicated audio_task on Core 1 (Priority 3)
    if (!s_audio_cmd_queue) {
        s_audio_cmd_queue = xQueueCreate(4, sizeof(AudioCmd));
    }
    if (!s_audio_task_handle) {
        xTaskCreatePinnedToCore(
            audio_task_func,
            "audio_task",
            2048,
            nullptr,
            3, // Priority 3
            &s_audio_task_handle,
            1  // Core 1
        );
    }

    Serial.printf("AUDIO: I2S DMA engine started (audio_task Core 1 Pri 3), output=%s vol=%u%% eq=%d/%d/%d\n",
                  s_output == AUDIO_OUT_CYD ? "CYD" : "phone",
                  (unsigned)s_volume_pct, (int)s_eq_db[0], (int)s_eq_db[1], (int)s_eq_db[2]);
}

void audio_hal_set_output(audio_out_t out) {
    if (out == s_output) return;
    if (out != AUDIO_OUT_PHONE && out != AUDIO_OUT_CYD) return;
    s_output = out;
    if (out == AUDIO_OUT_PHONE) {
        audio_hal_clamp_zero_volts();
    }
    savePrefs();
}

audio_out_t audio_hal_get_output(void) { return s_output; }

void audio_hal_set_volume_pct(uint32_t pct) {
    s_volume_pct = (pct > AUDIO_MAX_VOLUME_PCT) ? AUDIO_MAX_VOLUME_PCT : pct;
    savePrefs();
}

uint32_t audio_hal_get_volume_pct(void) { return s_volume_pct; }

void audio_hal_set_eq(int8_t bass_db, int8_t mid_db, int8_t treble_db) {
    auto clamp = [](int8_t v) { return (int8_t)(v < -12 ? -12 : (v > 12 ? 12 : v)); };
    s_eq_db[0] = clamp(bass_db);
    s_eq_db[1] = clamp(mid_db);
    s_eq_db[2] = clamp(treble_db);
    savePrefs();
}

void audio_hal_get_eq(int8_t *bass_db, int8_t *mid_db, int8_t *treble_db) {
    if (bass_db) *bass_db = s_eq_db[0];
    if (mid_db) *mid_db = s_eq_db[1];
    if (treble_db) *treble_db = s_eq_db[2];
}

const char *audio_hal_cycle_eq_preset(void) {
    struct Preset { int8_t b, m, t; const char *name; };
    static const Preset kPresets[] = {
        {0, 0, 0, "Flat"}, {6, 0, -2, "Bass"}, {-2, 2, 6, "Treble"},
    };
    for (int i = 0; i < 3; ++i) {
        if (s_eq_db[0] == kPresets[i].b && s_eq_db[1] == kPresets[i].m &&
            s_eq_db[2] == kPresets[i].t) {
            const Preset &next = kPresets[(i + 1) % 3];
            audio_hal_set_eq(next.b, next.m, next.t);
            return next.name;
        }
    }
    audio_hal_set_eq(0, 0, 0);
    return kPresets[0].name;
}

void audio_hal_play_alert(void) {
    if (s_output != AUDIO_OUT_CYD || !s_audio_cmd_queue) return;
    AudioCmd cmd = { TONE_ALERT };
    xQueueSend(s_audio_cmd_queue, &cmd, 0);
}

void audio_hal_play_click(void) {
    if (s_output != AUDIO_OUT_CYD || !s_audio_cmd_queue) return;
    if (s_playing) return; // Drop click feedback if an alert is already actively playing
    AudioCmd cmd = { TONE_CLICK };
    xQueueSend(s_audio_cmd_queue, &cmd, 0);
}

void audio_hal_play_beep(void) {
    if (s_output != AUDIO_OUT_CYD || !s_audio_cmd_queue) return;
    AudioCmd cmd = { TONE_BEEP };
    xQueueSend(s_audio_cmd_queue, &cmd, 0);
}

bool audio_hal_is_playing(void) {
    return s_playing;
}

#else // host / native tests

static audio_out_t s_output = AUDIO_OUT_PHONE;
static uint32_t s_volume_pct = 80;
static int8_t s_eq_db[3] = {0, 0, 0};
static bool s_a2dp_streaming = false;

void audio_hal_init(void) {}
void audio_hal_set_output(audio_out_t out) { s_output = out; }
audio_out_t audio_hal_get_output(void) { return s_output; }
void audio_hal_set_volume_pct(uint32_t pct) {
    s_volume_pct = (pct > AUDIO_MAX_VOLUME_PCT) ? AUDIO_MAX_VOLUME_PCT : pct;
}
uint32_t audio_hal_get_volume_pct(void) { return s_volume_pct; }
void audio_hal_set_eq(int8_t b, int8_t m, int8_t t) {
    auto clamp = [](int8_t v) { return (int8_t)(v < -12 ? -12 : (v > 12 ? 12 : v)); };
    s_eq_db[0] = clamp(b); s_eq_db[1] = clamp(m); s_eq_db[2] = clamp(t);
}
void audio_hal_get_eq(int8_t *b, int8_t *m, int8_t *t) {
    if (b) *b = s_eq_db[0]; if (m) *m = s_eq_db[1]; if (t) *t = s_eq_db[2];
}
const char *audio_hal_cycle_eq_preset(void) {
    struct Preset { int8_t b, m, t; const char *name; };
    static const Preset kPresets[] = {
        {0, 0, 0, "Flat"}, {6, 0, -2, "Bass"}, {-2, 2, 6, "Treble"},
    };
    for (int i = 0; i < 3; ++i) {
        if (s_eq_db[0] == kPresets[i].b && s_eq_db[1] == kPresets[i].m &&
            s_eq_db[2] == kPresets[i].t) {
            const Preset &next = kPresets[(i + 1) % 3];
            audio_hal_set_eq(next.b, next.m, next.t);
            return next.name;
        }
    }
    audio_hal_set_eq(0, 0, 0);
    return kPresets[0].name;
}
void audio_hal_play_alert(void) {}
void audio_hal_play_click(void) {}
void audio_hal_play_beep(void) {}
bool audio_hal_is_playing(void) { return false; }
void audio_hal_set_duck_callback(audio_duck_cb_t cb) { (void)cb; }
void audio_hal_clamp_zero_volts(void) {}
void audio_hal_idle_state(void) {}
void audio_hal_notify_a2dp_state(bool is_streaming) { s_a2dp_streaming = is_streaming; }
bool audio_hal_is_a2dp_streaming(void) { return s_a2dp_streaming; }

#endif // ESP_PLATFORM || ARDUINO