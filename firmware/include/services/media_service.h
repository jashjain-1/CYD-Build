#pragma once
#include <Arduino.h>

namespace svc {

enum class MediaState : uint8_t { Stopped = 0, Playing, Paused };

struct MediaInfo {
  bool connected;
  const char* title;
  const char* artist;
  const char* album;
  int index;
  int count;
  MediaState state;
  int volume;            // 0..10
  uint32_t positionMs;   // progress
  uint32_t durationMs;   // total duration
};

void mediaBegin();
void mediaNext();
void mediaPrev();
void mediaToggle();      // play/pause
void mediaVolumeUp();
void mediaVolumeDown();
void mediaTick();        // non-blocking: progress + short tone feedback

bool mediaIsConnected();
MediaInfo mediaInfo();
void simMediaSetConnected(bool connected);

}  // namespace svc
