#include "hal/audio_hal.h"
#include "config_build.h"
#include "config_pins.h"

namespace hal {
void audioBegin() {
#if SIM_BUILD
  Serial.println(F("[SIM] audio event log active"));
#elif AUDIO_ENABLED
  ledcSetup(0, 2000, 8);
  ledcAttachPin(PIN_AUDIO, 0);
  ledcWrite(0, 0);
#else
  pinMode(PIN_AUDIO, INPUT);
#endif
}

void audioTone(uint16_t freqHz, uint8_t volume) {
  if (volume == 0 || freqHz == 0) { audioStop(); return; }
#if SIM_BUILD
  Serial.printf("[SIM][AUDIO] tone %u Hz, vol %u/10\n", freqHz, volume);
#elif AUDIO_ENABLED
  const int level = constrain(static_cast<int>(volume), 0, 10);
  ledcWriteTone(0, constrain(static_cast<int>(freqHz), 100, 8000));
  // Duty changes timbre and level; this is NOT a speaker power limiter.
  ledcWrite(0, map(level, 0, 10, 0, 64));
#else
  (void)freqHz;
  (void)volume;
#endif
}

void audioStop() {
#if SIM_BUILD
  Serial.println(F("[SIM][AUDIO] off"));
#elif AUDIO_ENABLED
  ledcWrite(0, 0);
#endif
}
}  // namespace hal
