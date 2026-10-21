// SPDX-License-Identifier: MIT
// lynx firmware: pan/tilt servos, camera, and the LAN control layer (REST, MCP, MJPEG stream)
// for the XIAO ESP32-S3 Sense. Hardware glue only; motion, MCP and auth logic live in lib/
// (unit-tested natively).
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <freertos/semphr.h>

#include "app.h"
#include "camera.h"
#include "net.h"
#include "web.h"

using namespace lynx;

namespace lynx {
Settings settings;
Motion motion;
static SemaphoreHandle_t motionMutex;
void lockBegin() { motionMutex = xSemaphoreCreateRecursiveMutex(); }
MotionLock::MotionLock() { xSemaphoreTakeRecursive(motionMutex, portMAX_DELAY); }
MotionLock::~MotionLock() { xSemaphoreGiveRecursive(motionMutex); }
}  // namespace lynx

static const int kPin[AXIS_COUNT] = {D0, D1};  // pan, tilt
static const char* kName[AXIS_COUNT] = {"pan", "tilt"};
static const float kMechLimit[AXIS_COUNT] = {90.f, 60.f};  // validated in CAD

static Servo servo[AXIS_COUNT];
static bool servoAttached[AXIS_COUNT] = {false, false};
static int lastUs[AXIS_COUNT] = {-1, -1};
static String line;
static String cameraError;

// ---------- hardware ----------

static void driveServos() {
  for (int i = 0; i < AXIS_COUNT; i++) {
    const AxisMotion& ax = motion.axis((Axis)i);
    if (ax.attached()) {
      if (!servoAttached[i]) {
        servo[i].setPeriodHertz(50);
        servo[i].attach(kPin[i], (int)ax.cal().minUs, (int)ax.cal().maxUs);
        servoAttached[i] = true;
        lastUs[i] = -1;
      }
      int us = (int)lroundf(ax.pulseUs());
      if (us != lastUs[i]) {
        servo[i].writeMicroseconds(us);
        lastUs[i] = us;
      }
    } else if (servoAttached[i]) {
      servo[i].detach();  // stop PWM: no hum, no jitter at rest
      servoAttached[i] = false;
    }
  }
}

static void applyCal() {
  for (int i = 0; i < AXIS_COUNT; i++) motion.axis((Axis)i).setCal(settings.cal[i]);
  motion.config() = settings.motion;
}

// Re-issue the current target so a calibration change is visible immediately.
static void refresh() {
  motion.look(motion.axis(PAN).target(), motion.axis(TILT).target(), 300, millis());
}

// ---------- status ----------

namespace lynx {

void statusJson(JsonObject j) {
  {
    MotionLock l;
    for (int i = 0; i < AXIS_COUNT; i++) {
      const AxisMotion& ax = motion.axis((Axis)i);
      JsonObject o = j[kName[i]].to<JsonObject>();
      o["pos"] = ax.position();
      o["target"] = ax.target();
      o["us"] = (int)lroundf(ax.pulseUs());
      o["moving"] = ax.moving();
      o["attached"] = ax.attached();
      o["min"] = ax.cal().minDeg;
      o["max"] = ax.cal().maxDeg;
      o["trim"] = ax.cal().trimUs;
      o["usPerDeg"] = ax.cal().usPerDeg;
      o["invert"] = ax.cal().invert;
    }
    j["sweeping"] = motion.sweeping();
  }
  j["restTilt"] = Motion::kRestTiltDeg;
  j["idleMs"] = settings.motion.idleDetachMs;
  j["maxSpeed"] = settings.motion.maxSpeedDegPerS;
  cameraJson(j["camera"].to<JsonObject>());
  JsonObject n = j["network"].to<JsonObject>();
  n["name"] = hostName();
  n["id"] = deviceId();
  n["mdns"] = mdnsName();
  n["ip"] = staIp();
  n["wifi"] = wifiState();
  n["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  n["setupHotspot"] = setupMode();
  if (setupMode()) n["setupClosesInMs"] = setupClosesInMs();
  j["version"] = kVersion;
  j["uptimeMs"] = millis();
  j["freeHeap"] = ESP.getFreeHeap();
  j["freePsram"] = ESP.getFreePsram();
}

void cameraJson(JsonObject o) {
  o["ready"] = cameraReady();
  if (!cameraReady()) o["error"] = cameraError;
  o["framesize"] = framesizeName(settings.camera.framesize);
  o["quality"] = settings.camera.quality;
  o["vflip"] = settings.camera.vflip;
  o["hmirror"] = settings.camera.hmirror;
  o["brightness"] = settings.camera.brightness;
}

bool applyCameraJson(JsonObjectConst in, std::string& error) {
  CameraSettings c = settings.camera;
  if (!in["framesize"].isNull()) {
    int fs = framesizeFromName(in["framesize"].as<const char*>());
    if (fs < 0) {
      error = "framesize must be one of QVGA, VGA, SVGA, XGA, HD, SXGA, UXGA";
      return false;
    }
    c.framesize = fs;
  }
  if (!in["quality"].isNull()) {
    int q = in["quality"].as<int>();
    if (q < 4 || q > 63) {
      error = "quality must be 4..63 (lower is better)";
      return false;
    }
    c.quality = q;
  }
  if (!in["brightness"].isNull()) {
    int b = in["brightness"].as<int>();
    if (b < -2 || b > 2) {
      error = "brightness must be -2..2";
      return false;
    }
    c.brightness = b;
  }
  if (!in["vflip"].isNull()) c.vflip = in["vflip"].as<bool>();
  if (!in["hmirror"].isNull()) c.hmirror = in["hmirror"].as<bool>();
  settings.camera = c;
  cameraApply(c);
  saveCamera(c);
  return true;
}

}  // namespace lynx

// ---------- serial commands ----------

static int parseAxis(const String& s) {
  if (s == "pan" || s == "p") return PAN;
  if (s == "tilt" || s == "t") return TILT;
  return -1;
}

static void printHelp() {
  Serial.println(F(
      "lynx commands:\n"
      "  pan <deg>                 move pan (tilt unchanged)\n"
      "  tilt <deg>                move tilt (pan unchanged)\n"
      "  look <pan> <tilt> [ms]    move both, optional duration\n"
      "  center                    return to rest (pan 0, tilt 45)\n"
      "  sweep                     demo the full soft-limit range\n"
      "  limits                    show soft limits\n"
      "  limits <axis> <min> <max> set soft limits (inside mechanical +/-90 pan, +/-60 tilt)\n"
      "  trim <axis> <us>          centre pulse width (e.g. 1500)\n"
      "  usperdeg <axis> <us>      pulse per degree (default 11.11)\n"
      "  invert <axis>             toggle direction\n"
      "  speed <deg/s>             max angular speed\n"
      "  idle <ms>                 detach PWM after idle (0 = never)\n"
      "  wifi <ssid> <pass>        save Wi-Fi credentials and reconnect\n"
      "  setup [s]                 reopen the setup hotspot (default 10 minutes)\n"
      "  token                     print the API token\n"
      "  token new                 mint a new API token (old one stops working)\n"
      "  info                      name, address, version\n"
      "  save                      persist calibration/limits to NVS\n"
      "  status                    print JSON status\n"
      "  help"));
}

static void handleCommand(String cmd) {
  cmd.trim();
  if (!cmd.length()) return;
  String tok[5];
  int n = 0, from = 0;
  while (n < 5 && from < (int)cmd.length()) {
    int sp = cmd.indexOf(' ', from);
    if (sp < 0) sp = cmd.length();
    if (sp > from) tok[n++] = cmd.substring(from, sp);
    from = sp + 1;
  }
  // wifi keeps the remainder as the password (may contain spaces)
  String c = tok[0];
  c.toLowerCase();
  uint32_t now = millis();
  MotionLock lock;  // the HTTP tasks touch motion too

  if (c == "help" || c == "?") {
    printHelp();
  } else if (c == "pan" && n >= 2) {
    motion.look(tok[1].toFloat(), motion.axis(TILT).target(), -1, now);
  } else if (c == "tilt" && n >= 2) {
    motion.look(motion.axis(PAN).target(), tok[1].toFloat(), -1, now);
  } else if (c == "look" && n >= 3) {
    uint32_t d = motion.look(tok[1].toFloat(), tok[2].toFloat(), n >= 4 ? tok[3].toInt() : -1, now);
    Serial.printf("ok %u ms\n", (unsigned)d);
  } else if (c == "center" || c == "centre") {
    motion.center(now);
  } else if (c == "sweep") {
    motion.startSweep(now);
  } else if (c == "limits" && n == 1) {
    for (int i = 0; i < AXIS_COUNT; i++)
      Serial.printf("%s: %.1f .. %.1f deg (mechanical +/-%.0f)\n", kName[i], settings.cal[i].minDeg,
                    settings.cal[i].maxDeg, kMechLimit[i]);
  } else if (c == "limits" && n >= 4 && parseAxis(tok[1]) >= 0) {
    int a = parseAxis(tok[1]);
    float lo = constrain(tok[2].toFloat(), -kMechLimit[a], 0.f);
    float hi = constrain(tok[3].toFloat(), 0.f, kMechLimit[a]);
    settings.cal[a].minDeg = lo;
    settings.cal[a].maxDeg = hi;
    applyCal();
    Serial.printf("%s limits %.1f .. %.1f (unsaved)\n", kName[a], lo, hi);
  } else if (c == "trim" && n >= 3 && parseAxis(tok[1]) >= 0) {
    int a = parseAxis(tok[1]);
    settings.cal[a].trimUs = constrain(tok[2].toFloat(), settings.cal[a].minUs, settings.cal[a].maxUs);
    applyCal();
    refresh();
    Serial.printf("%s trim %.0f us (unsaved)\n", kName[a], settings.cal[a].trimUs);
  } else if (c == "usperdeg" && n >= 3 && parseAxis(tok[1]) >= 0) {
    int a = parseAxis(tok[1]);
    settings.cal[a].usPerDeg = constrain(tok[2].toFloat(), 1.f, 30.f);
    applyCal();
    refresh();
    Serial.printf("%s %.2f us/deg (unsaved)\n", kName[a], settings.cal[a].usPerDeg);
  } else if (c == "invert" && n >= 2 && parseAxis(tok[1]) >= 0) {
    int a = parseAxis(tok[1]);
    settings.cal[a].invert = !settings.cal[a].invert;
    applyCal();
    refresh();
    Serial.printf("%s invert %s (unsaved)\n", kName[a], settings.cal[a].invert ? "on" : "off");
  } else if (c == "speed" && n >= 2) {
    settings.motion.maxSpeedDegPerS = constrain(tok[1].toFloat(), 10.f, 600.f);
    applyCal();
  } else if (c == "idle" && n >= 2) {
    settings.motion.idleDetachMs = (uint32_t)max(0L, tok[1].toInt());
    applyCal();
  } else if (c == "save") {
    saveSettings(settings);
    Serial.println("saved");
  } else if (c == "status") {
    JsonDocument j;
    statusJson(j.to<JsonObject>());
    serializeJson(j, Serial);
    Serial.println();
  } else if (c == "wifi" && n >= 2) {
    int p = cmd.indexOf(tok[1]) + tok[1].length();
    String pass = cmd.substring(p);
    pass.trim();
    joinWifi(tok[1], pass);
  } else if (c == "setup") {
    enterSetupMode(n >= 2 ? (uint32_t)tok[1].toInt() * 1000 : 0);
  } else if (c == "token" && n >= 2 && tok[1] == "new") {
    settings.token = newToken();
    saveToken(settings.token);
    Serial.printf("new token: %s\n", settings.token.c_str());
  } else if (c == "token") {
    Serial.printf("token: %s\n", settings.token.c_str());
  } else if (c == "info") {
    Serial.printf("lynx %s  %s  http://%s/  ip %s  wifi %s%s\n", kVersion, hostName().c_str(), mdnsName().c_str(),
                  staIp().length() ? staIp().c_str() : "-", wifiState(), setupMode() ? "  (setup hotspot up)" : "");
    if (!cameraReady()) Serial.printf("camera: %s\n", cameraError.c_str());
  } else {
    Serial.printf("? unknown or incomplete: '%s' (try help)\n", cmd.c_str());
  }
}

static void pollSerial() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (line.length()) handleCommand(line);
      line = "";
    } else if (line.length() < 160) {
      line += ch;
    }
  }
}

// ---------- entry points ----------

void setup() {
  Serial.begin(115200);
  lockBegin();
  // servos own LEDC timers 0-1; the camera clock is on timer 3 (camera.cpp)
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  loadSettings(settings);
  motion.begin(settings.cal[PAN], settings.cal[TILT], settings.motion, millis());
  motion.center(millis());  // boot into the rest pose

  if (!cameraBegin(settings.camera, cameraError)) Serial.printf("camera: %s\n", cameraError.c_str());
  netBegin();
  webBegin();
  Serial.printf("lynx %s ready as %s (type 'help')\n", kVersion, hostName().c_str());
}

void loop() {
  uint32_t now = millis();
  pollSerial();
  netLoop();
  {
    MotionLock l;
    motion.update(now);
    driveServos();
  }
  delay(2);
}
