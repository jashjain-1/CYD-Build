#pragma once
#include "services/nav_service.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace phonecue {
struct Cue {
  svc::TurnIcon icon;
  char distance[16], street[32], eta[16];
  float speed;
};

// CUE LEFT|250 m|MG Road|12 min|40 (ASCII, five required fields).
inline bool parse(const char* input, Cue& cue) {
  if (!input) return false;
  char fields[5][32] = {};
  unsigned field = 0, offset = 0;
  for (const char* p = input; *p; ++p) {
    if (*p == '|') {
      if (!offset || field == 4) return false;
      ++field; offset = 0;
    } else {
      if (static_cast<unsigned char>(*p) < 32 || static_cast<unsigned char>(*p) > 126 || offset >= 31)
        return false;
      fields[field][offset++] = *p;
    }
  }
  if (field != 4 || !offset || std::strlen(fields[1]) >= sizeof(cue.distance) ||
      std::strlen(fields[3]) >= sizeof(cue.eta)) return false;
  const char* names[] = {"STRAIGHT", "LEFT", "RIGHT", "SLEFT", "SRIGHT", "UTURN", "ROUND", "ARRIVE"};
  unsigned icon = 0;
  while (icon < 8 && std::strcmp(fields[0], names[icon])) ++icon;
  if (icon == 8) return false;
  char* end = nullptr;
  const float speed = std::strtof(fields[4], &end);
  if (end == fields[4] || *end || !std::isfinite(speed) || speed < 0 || speed > 300) return false;
  cue.icon = static_cast<svc::TurnIcon>(icon);
  std::strcpy(cue.distance, fields[1]);
  std::strcpy(cue.street, fields[2]);
  std::strcpy(cue.eta, fields[3]);
  cue.speed = speed;
  return true;
}
}  // namespace phonecue
