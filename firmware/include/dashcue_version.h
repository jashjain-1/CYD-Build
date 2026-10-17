// DashCue firmware version identity.
//
// This header is the single source of truth for the versions the HUD reports
// over HELLO/DEVICE_STATUS. Bump rules:
//   * FW_VERSION_*  -> any behaviour change; shown on the HUD Settings page
//   * PROTO_MAJOR   -> incompatible wire change (a receiver MUST reject it)
//   * PROTO_MINOR   -> additive wire change (effective version = min(both))
// Changing PROTO_* requires updating protocol/dashcue.proto in the same commit;
// tools/check_proto_version.py fails the build if the two disagree.
#pragma once

#define DASHCUE_FW_VERSION_MAJOR 0
#define DASHCUE_FW_VERSION_MINOR 1
#define DASHCUE_FW_VERSION_PATCH 0

// Keep in sync with protocol/dashcue.proto (package dashcue.v1 => 1.0).
#define DASHCUE_PROTO_MAJOR 1
#define DASHCUE_PROTO_MINOR 0

#define DASHCUE_STRINGIFY_(x) #x
#define DASHCUE_STRINGIFY(x) DASHCUE_STRINGIFY_(x)

#define DASHCUE_FW_VERSION_STRING          \
  DASHCUE_STRINGIFY(DASHCUE_FW_VERSION_MAJOR) "." \
  DASHCUE_STRINGIFY(DASHCUE_FW_VERSION_MINOR) "." \
  DASHCUE_STRINGIFY(DASHCUE_FW_VERSION_PATCH)
