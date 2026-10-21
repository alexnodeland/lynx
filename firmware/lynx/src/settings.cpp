// SPDX-License-Identifier: MIT
#include "settings.h"

#include <Preferences.h>
#include <esp_random.h>

#include "auth.h"

namespace lynx {

static const char* kNs = "lynx";
static const char* kAxisKey[AXIS_COUNT] = {"p", "t"};

static String k(Axis a, const char* f) { return String(kAxisKey[a]) + f; }

// Preferences logs an error for every missing key; on a fresh board that is all of them.
static float getF(Preferences& p, const char* key, float d) { return p.isKey(key) ? p.getFloat(key, d) : d; }
static bool getB(Preferences& p, const char* key, bool d) { return p.isKey(key) ? p.getBool(key, d) : d; }
static int getI(Preferences& p, const char* key, int d) { return p.isKey(key) ? p.getInt(key, d) : d; }
static uint32_t getU(Preferences& p, const char* key, uint32_t d) { return p.isKey(key) ? p.getUInt(key, d) : d; }
static String getS(Preferences& p, const char* key) { return p.isKey(key) ? p.getString(key, "") : String(); }

void loadSettings(Settings& s) {
  Preferences p;
  p.begin(kNs, true);
  for (int i = 0; i < AXIS_COUNT; i++) {
    Axis a = (Axis)i;
    AxisCal d = defaultAxisCal(a);
    AxisCal& c = s.cal[i];
    c.minDeg = getF(p, k(a, "min").c_str(), d.minDeg);
    c.maxDeg = getF(p, k(a, "max").c_str(), d.maxDeg);
    c.trimUs = getF(p, k(a, "trim").c_str(), d.trimUs);
    c.usPerDeg = getF(p, k(a, "upd").c_str(), d.usPerDeg);
    c.invert = getB(p, k(a, "inv").c_str(), d.invert);
    c.minUs = getF(p, k(a, "minus").c_str(), d.minUs);
    c.maxUs = getF(p, k(a, "maxus").c_str(), d.maxUs);
  }
  MotionConfig m = defaultMotionConfig();
  s.motion = m;
  s.motion.maxSpeedDegPerS = getF(p, "speed", m.maxSpeedDegPerS);
  s.motion.idleDetachMs = getU(p, "idle", m.idleDetachMs);
  s.ssid = getS(p, "ssid");
  s.pass = getS(p, "pass");
  s.token = getS(p, "token");
  s.camera.framesize = getI(p, "cfs", s.camera.framesize);
  s.camera.quality = getI(p, "cq", s.camera.quality);
  s.camera.vflip = getB(p, "cvf", s.camera.vflip);
  s.camera.hmirror = getB(p, "chm", s.camera.hmirror);
  s.camera.brightness = getI(p, "cbr", s.camera.brightness);
  p.end();
  if (s.token.length() != 32) {
    s.token = newToken();
    saveToken(s.token);
  }
}

String newToken() {
  uint8_t b[16];
  esp_fill_random(b, sizeof b);
  return String(tokenFromBytes(b).c_str());
}

void saveToken(const String& token) {
  Preferences p;
  p.begin(kNs, false);
  p.putString("token", token);
  p.end();
}

void saveCamera(const CameraSettings& c) {
  Preferences p;
  p.begin(kNs, false);
  p.putInt("cfs", c.framesize);
  p.putInt("cq", c.quality);
  p.putBool("cvf", c.vflip);
  p.putBool("chm", c.hmirror);
  p.putInt("cbr", c.brightness);
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
