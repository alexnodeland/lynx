// SPDX-License-Identifier: MIT
// NVS persistence (Preferences) for calibration, limits, timing and Wi-Fi.
#pragma once
#include <Arduino.h>

#include "motion.h"

namespace lynx {

struct Settings {
  AxisCal cal[AXIS_COUNT];
  MotionConfig motion;
  String ssid, pass;
};

void loadSettings(Settings& s);   // falls back to defaults per key
void saveSettings(const Settings& s);
void saveWifi(const String& ssid, const String& pass);

}  // namespace lynx
