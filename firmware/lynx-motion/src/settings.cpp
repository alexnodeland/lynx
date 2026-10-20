// SPDX-License-Identifier: MIT
#include "settings.h"

#include <Preferences.h>

namespace lynx {

static const char* kNs = "lynx";
static const char* kAxisKey[AXIS_COUNT] = {"p", "t"};

static String k(Axis a, const char* f) { return String(kAxisKey[a]) + f; }

void loadSettings(Settings& s) {
  Preferences p;
  p.begin(kNs, true);
  for (int i = 0; i < AXIS_COUNT; i++) {
    Axis a = (Axis)i;
    AxisCal d = defaultAxisCal(a);
    AxisCal& c = s.cal[i];
    c.minDeg = p.getFloat(k(a, "min").c_str(), d.minDeg);
    c.maxDeg = p.getFloat(k(a, "max").c_str(), d.maxDeg);
    c.trimUs = p.getFloat(k(a, "trim").c_str(), d.trimUs);
    c.usPerDeg = p.getFloat(k(a, "upd").c_str(), d.usPerDeg);
    c.invert = p.getBool(k(a, "inv").c_str(), d.invert);
    c.minUs = p.getFloat(k(a, "minus").c_str(), d.minUs);
    c.maxUs = p.getFloat(k(a, "maxus").c_str(), d.maxUs);
  }
  MotionConfig m = defaultMotionConfig();
  s.motion = m;
  s.motion.maxSpeedDegPerS = p.getFloat("speed", m.maxSpeedDegPerS);
  s.motion.idleDetachMs = p.getUInt("idle", m.idleDetachMs);
  s.ssid = p.getString("ssid", "");
  s.pass = p.getString("pass", "");
  p.end();
}

void saveSettings(const Settings& s) {
  Preferences p;
  p.begin(kNs, false);
  for (int i = 0; i < AXIS_COUNT; i++) {
    Axis a = (Axis)i;
    const AxisCal& c = s.cal[i];
    p.putFloat(k(a, "min").c_str(), c.minDeg);
    p.putFloat(k(a, "max").c_str(), c.maxDeg);
    p.putFloat(k(a, "trim").c_str(), c.trimUs);
    p.putFloat(k(a, "upd").c_str(), c.usPerDeg);
    p.putBool(k(a, "inv").c_str(), c.invert);
    p.putFloat(k(a, "minus").c_str(), c.minUs);
    p.putFloat(k(a, "maxus").c_str(), c.maxUs);
  }
  p.putFloat("speed", s.motion.maxSpeedDegPerS);
  p.putUInt("idle", s.motion.idleDetachMs);
  p.end();
}

void saveWifi(const String& ssid, const String& pass) {
  Preferences p;
  p.begin(kNs, false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.end();
}

}  // namespace lynx
