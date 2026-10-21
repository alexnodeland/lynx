// SPDX-License-Identifier: MIT
// NVS persistence (Preferences) for calibration, limits, timing, Wi-Fi, the API token and camera settings.
#pragma once
#include <Arduino.h>

#include "motion.h"
#include "presets.h"

namespace lynx {

struct CameraSettings {
  int framesize = 8;  // FRAMESIZE_VGA (640x480)
  int quality = 12;   // JPEG quality, lower is better
  bool vflip = false;
  bool hmirror = false;
  int brightness = 0;
};

struct Settings {
  AxisCal cal[AXIS_COUNT];
  MotionConfig motion;
  String ssid, pass;
  String token;  // per-device API token, generated on first boot
  CameraSettings camera;
  // detection + follow (detect.cpp, lib/follow)
  bool detect = false;
  int detectModel = 0;  // DETECT_FACE | DETECT_CAT
  int detectRange = 1;  // RANGE_NEAR | RANGE_FAR
  bool follow = false;
  int followPanSign = -1, followTiltSign = 1;  // for how the board sits in the pod
};

void loadSettings(Settings& s);  // falls back to defaults per key; mints the token if absent
void saveSettings(const Settings& s);
void saveWifi(const String& ssid, const String& pass);
void saveToken(const String& token);
void saveCamera(const CameraSettings& c);
void saveDetect(const Settings& s);
// presets and the patrol live in their own keys (blobs)
void loadPresets(Presets& p);
void savePresets(const Presets& p);
struct PatrolSaved {
  bool on = false;
  int n = 0;
  int order[kMaxPresets] = {};
  uint32_t dwellMs = 10000;
};
void loadPatrol(PatrolSaved& p);
void savePatrol(const PatrolSaved& p);
String newToken();  // 32 hex chars from the hardware RNG

}  // namespace lynx
