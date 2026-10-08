#pragma once
#include <Arduino.h>

namespace svc {

struct SystemInfo {
  uint32_t uptimeS;
  uint32_t freeHeap;
  bool simBuild;
  const char* fwName;
  const char* fwVersion;
};

void systemBegin();
void systemTick();       // refreshes cached heap ~5 s
SystemInfo systemInfo();

}  // namespace svc
