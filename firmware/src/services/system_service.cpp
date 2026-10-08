#include "services/system_service.h"
#include "config_build.h"

namespace {
uint32_t s_bootMs = 0;
uint32_t s_freeHeap = 0;
uint32_t s_lastHeapMs = 0;
}  // namespace

namespace svc {

void systemBegin() {
  s_bootMs = millis();
  s_lastHeapMs = millis();
  s_freeHeap = ESP.getFreeHeap();
}

void systemTick() {
  const uint32_t now = millis();
  if ((int32_t)(now - s_lastHeapMs) >= 5000) {
    s_lastHeapMs = now;
    s_freeHeap = ESP.getFreeHeap();
  }
}

SystemInfo systemInfo() {
  SystemInfo s;
  s.uptimeS = (millis() - s_bootMs) / 1000;
  s.freeHeap = s_freeHeap;
  s.simBuild = (SIM_BUILD != 0);
  s.fwName = FW_NAME;
  s.fwVersion = FW_VERSION;
  return s;
}

}  // namespace svc
