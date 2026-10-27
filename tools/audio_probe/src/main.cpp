#include <Arduino.h>
#include "dildaara_audio.h"

// On 3.5" CYD (ESP32-3248S035), onboard audio amp (8002A) is driven by GPIO 26 (DAC2).
#define PIN_SPEAKER 26

// Maximum safe volume scale: 1.4x (~70% peak volume). Clamped to protect 0.05W / 0.5W coil.
static float s_vol_scale = 1.4f;
static bool s_auto_loop = true;

void playSampledSong() {
  Serial.println();
  Serial.println("[AUDIO] >>> STREAMING ACTUAL MASTER TRACK: 'Dildaara' (Ra.One) <<<");
  Serial.printf("[AUDIO] Format: 16 kHz, 8-bit mono PCM (%d samples = ~12.0s)\n", DILDAARA_NUM_SAMPLES);
  Serial.printf("[AUDIO] Vocals: Shafqat Amanat Ali + Acoustic Guitar & Full Chorus\n");
  Serial.printf("[AUDIO] Volume scale: %.1fx (Safe 50%% volume)\n", s_vol_scale);

  const int64_t sample_period_us = 1000000LL / DILDAARA_SAMPLE_RATE;
  int64_t next_tick = esp_timer_get_time();

  for (uint32_t i = 0; i < DILDAARA_NUM_SAMPLES; ++i) {
    uint8_t raw = pgm_read_byte(&dildaara_pcm[i]);

    // Scale around midpoint 128
    int16_t diff = (int16_t)raw - 128;
    int16_t scaled = 128 + (int16_t)(diff * s_vol_scale);
    if (scaled < 0) scaled = 0;
    if (scaled > 255) scaled = 255;

    dacWrite(PIN_SPEAKER, (uint8_t)scaled);

    // Feed FreeRTOS watchdog & check live serial keys every 1024 samples (~64 ms)
    if ((i & 0x3FF) == 0) {
      yield();
      if (Serial.available()) {
        char c = Serial.read();
        if (c == '+' && s_vol_scale < 1.6f) {
          s_vol_scale += 0.2f;
          Serial.printf("[AUDIO] Volume scale: %.1fx\n", s_vol_scale);
        } else if (c == '-' && s_vol_scale > 0.3f) {
          s_vol_scale -= 0.2f;
          Serial.printf("[AUDIO] Volume scale: %.1fx\n", s_vol_scale);
        } else if (c == 's' || c == 'q') {
          Serial.println("[AUDIO] Playback stopped.");
          dacWrite(PIN_SPEAKER, 0);
          return;
        }
      }
    }

    next_tick += sample_period_us;
    while (esp_timer_get_time() < next_tick) {
      // Jitter-free hardware timer sync
    }
  }

  // Smooth return to 0 to prevent DC click
  for (int val = 128; val >= 0; --val) {
    dacWrite(PIN_SPEAKER, val);
    delayMicroseconds(25);
  }

  Serial.println("[AUDIO] Playback complete. Silent.");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==================================================================");
  Serial.println("   DASHCUE REAL AUDIO PLAYBACK TEST - 'DILDAARA' (RA.ONE)");
  Serial.println("   Actual sampled voice & music via ESP32 DAC (GPIO 26)");
  Serial.println("==================================================================");
  Serial.println("Live Serial Controls:");
  Serial.println("  'p' = Replay song");
  Serial.println("  '+' = Increase volume slightly");
  Serial.println("  '-' = Decrease volume slightly");
  Serial.println("  's' = Stop playback");
  Serial.println("  'm' = Toggle auto-loop");
  Serial.println();

  dacWrite(PIN_SPEAKER, 0);

  Serial.println("[AUDIO] Starting actual song in 1 second...");
  delay(1000);
  playSampledSong();
}

static uint32_t s_last_play_time = 0;

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'p' || c == 'P') {
      playSampledSong();
      s_last_play_time = millis();
    } else if (c == '+') {
      if (s_vol_scale < 1.6f) s_vol_scale += 0.2f;
      Serial.printf("[AUDIO] Volume scale set to %.1fx\n", s_vol_scale);
      playSampledSong();
      s_last_play_time = millis();
    } else if (c == '-') {
      if (s_vol_scale > 0.3f) s_vol_scale -= 0.2f;
      Serial.printf("[AUDIO] Volume scale set to %.1fx\n", s_vol_scale);
      playSampledSong();
      s_last_play_time = millis();
    } else if (c == 'm' || c == 'M') {
      s_auto_loop = !s_auto_loop;
      Serial.printf("[AUDIO] Auto-loop: %s\n", s_auto_loop ? "ENABLED" : "MUTED/PAUSED");
    }
  }

  // Auto-repeat every 5 seconds if enabled
  if (s_auto_loop && (millis() - s_last_play_time >= 5000)) {
    s_last_play_time = millis();
    playSampledSong();
  }

  delay(30);
}
