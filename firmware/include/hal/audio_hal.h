#pragma once
#include <Arduino.h>

namespace hal {

void audioBegin();

// Start a square-wave tone (optional tone-output hardware path via LEDC).
// volume: 0..10; zero mutes. Hardware attenuation is required; PWM is not a limiter.
void audioTone(uint16_t freqHz, uint8_t volume);

void audioStop();

}  // namespace hal
