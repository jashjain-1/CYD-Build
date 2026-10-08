#include "services/media_service.h"
#include "config_build.h"
#include "hal/audio_hal.h"

namespace {
svc::MediaState s_mediaState = svc::MediaState::Stopped;
int s_volume = 3;
uint8_t s_blipIndex = 2;
uint16_t s_firstHz = 660, s_secondHz = 990, s_secondMs = 140;
uint32_t s_blipDueMs = 0;
#if SIM_BUILD
bool s_connected = true;
int s_demoIndex = 0;
uint32_t s_positionMs = 0, s_lastTickMs = 0;
const char* const kTitles[] = {"Midnight Drive", "Cruising Bangalore", "Highway Serenade"};
#endif

void stopBlip() {
  s_blipIndex = 2;
  hal::audioStop();
}

void startBlip(uint16_t first, uint16_t second, uint16_t duration) {
  s_firstHz = first;
  s_secondHz = second;
  s_secondMs = duration;
  s_blipIndex = 0;
  s_blipDueMs = millis();  // safe even after millis() crosses 2^31
}

void tickBlip(uint32_t now) {
  if (s_blipIndex > 2 || static_cast<int32_t>(now - s_blipDueMs) < 0) return;
  if (s_blipIndex == 0) {
    hal::audioTone(s_firstHz, s_volume);
    s_blipDueMs = now + 90;
  } else if (s_blipIndex == 1) {
    hal::audioTone(s_secondHz, s_volume);
    s_blipDueMs = now + s_secondMs;
  } else {
    hal::audioStop();
#if !SIM_BUILD
    s_mediaState = svc::MediaState::Stopped;
#endif
  }
  ++s_blipIndex;
}
}  // namespace

namespace svc {
void mediaBegin() {
  s_mediaState = MediaState::Stopped;
  s_blipIndex = 3;
  hal::audioStop();
#if SIM_BUILD
  s_lastTickMs = millis();
  simMediaSetConnected(true);
#endif
}

void mediaTick() {
  const uint32_t now = millis();
#if SIM_BUILD
  const uint32_t dt = now - s_lastTickMs;
  s_lastTickMs = now;
  if (s_connected && s_mediaState == MediaState::Playing) {
    s_positionMs += dt;
    if (s_positionMs >= 180000) mediaNext();
  }
#endif
  tickBlip(now);
}

void mediaNext() {
#if SIM_BUILD
  if (!s_connected) return;
  s_demoIndex = (s_demoIndex + 1) % 3;
  s_positionMs = 0;
#endif
  startBlip(880, 1320, 130);
}
void mediaPrev() {
#if SIM_BUILD
  if (!s_connected) return;
  s_demoIndex = (s_demoIndex + 2) % 3;
  s_positionMs = 0;
#endif
  startBlip(1320, 880, 130);
}
void mediaToggle() {
#if SIM_BUILD
  if (!s_connected) return;
#endif
  if (s_mediaState == MediaState::Playing) {
    s_mediaState = MediaState::Paused;
    stopBlip();
    s_blipIndex = 3;
  } else {
    s_mediaState = MediaState::Playing;
    startBlip(660, 990, 140);
  }
}
void mediaVolumeUp() {
  if (s_volume < 10) ++s_volume;
  startBlip(988, 988, 80);
}
void mediaVolumeDown() {
  if (s_volume > 0) --s_volume;
  startBlip(784, 784, 80);
}

void simMediaSetConnected(bool connected) {
#if SIM_BUILD
  s_connected = connected;
  s_mediaState = connected ? MediaState::Playing : MediaState::Stopped;
  s_positionMs = 0;
  s_lastTickMs = millis();
  stopBlip();
  s_blipIndex = 3;
#else
  (void)connected;
#endif
}
bool mediaIsConnected() {
#if SIM_BUILD
  return s_connected;
#else
  return false;  // no phone transport is implemented
#endif
}
MediaInfo mediaInfo() {
  MediaInfo m = {};
  m.connected = mediaIsConnected();
  m.state = s_mediaState;
  m.volume = s_volume;
#if SIM_BUILD
  m.title = s_connected ? kTitles[s_demoIndex] : "Demo disconnected";
  m.artist = "Simulated player";
  m.index = s_demoIndex;
  m.count = 3;
  m.positionMs = s_positionMs;
  m.durationMs = s_connected ? 180000 : 0;
#else
  m.title = "Feedback tones";
  m.artist = AUDIO_ENABLED ? "Local output; no music playback" : "Audio output disabled";
#endif
  m.album = "";
  return m;
}
}  // namespace svc
