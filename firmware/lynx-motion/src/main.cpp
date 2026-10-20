// SPDX-License-Identifier: MIT
// lynx-motion: pan/tilt servo control for the XIAO ESP32-S3 Sense.
// Hardware glue only; motion logic lives in lib/motion (unit-tested natively).
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <WebServer.h>
#include <WiFi.h>

#include "motion.h"
#include "page.h"
#include "settings.h"

using namespace lynx;

static const int kPin[AXIS_COUNT] = {D0, D1};  // pan, tilt
static const char* kName[AXIS_COUNT] = {"pan", "tilt"};
static const float kMechLimit[AXIS_COUNT] = {90.f, 60.f};  // validated in CAD
static const uint32_t kStaConnectTimeoutMs = 15000;

static Settings settings;
static Motion motion;
static Servo servo[AXIS_COUNT];
static bool servoAttached[AXIS_COUNT] = {false, false};
static int lastUs[AXIS_COUNT] = {-1, -1};
static WebServer http(80);
static bool apStarted = false;
static uint32_t staStartedMs = 0;
static String line;

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

static void statusJson(JsonDocument& j) {
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
  j["idleMs"] = settings.motion.idleDetachMs;
  j["maxSpeed"] = settings.motion.maxSpeedDegPerS;
  j["wifi"] = WiFi.status() == WL_CONNECTED ? "sta" : (apStarted ? "ap" : "off");
  j["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  j["uptimeMs"] = millis();
}

// ---------- serial commands ----------

static int parseAxis(const String& s) {
  if (s == "pan" || s == "p") return PAN;
  if (s == "tilt" || s == "t") return TILT;
  return -1;
}

static void printHelp() {
  Serial.println(F(
      "lynx-motion commands:\n"
      "  pan <deg>                 move pan (tilt unchanged)\n"
      "  tilt <deg>                move tilt (pan unchanged)\n"
      "  look <pan> <tilt> [ms]    move both, optional duration\n"
      "  center                    return to 0,0\n"
      "  sweep                     demo the full soft-limit range\n"
      "  limits                    show soft limits\n"
      "  limits <axis> <min> <max> set soft limits (inside mechanical +/-90 pan, +/-60 tilt)\n"
      "  trim <axis> <us>          centre pulse width (e.g. 1500)\n"
      "  usperdeg <axis> <us>      pulse per degree (default 11.11)\n"
      "  invert <axis>             toggle direction\n"
      "  speed <deg/s>             max angular speed\n"
      "  idle <ms>                 detach PWM after idle (0 = never)\n"
      "  wifi <ssid> <pass>        save Wi-Fi credentials and reconnect\n"
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
    statusJson(j);
    serializeJson(j, Serial);
    Serial.println();
  } else if (c == "wifi" && n >= 2) {
    int p = cmd.indexOf(tok[1]) + tok[1].length();
    String pass = cmd.substring(p);
    pass.trim();
    settings.ssid = tok[1];
    settings.pass = pass;
    saveWifi(settings.ssid, settings.pass);
    Serial.printf("wifi saved, connecting to '%s'\n", settings.ssid.c_str());
    WiFi.disconnect();
    WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());
    staStartedMs = millis();
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

// ---------- HTTP ----------

static void sendStatus() {
  JsonDocument j;
  statusJson(j);
  String out;
  serializeJson(j, out);
  http.send(200, "application/json", out);
}

static void setupHttp() {
  http.on("/", HTTP_GET, [] { http.send_P(200, "text/html", kIndexHtml); });
  http.on("/api/status", HTTP_GET, sendStatus);
  http.on("/api/look", HTTP_POST, [] {
    JsonDocument b;
    if (deserializeJson(b, http.arg("plain"))) {
      http.send(400, "application/json", "{\"error\":\"bad json\"}");
      return;
    }
    float p = b["pan"] | motion.axis(PAN).target();
    float t = b["tilt"] | motion.axis(TILT).target();
    int32_t ms = b["ms"] | -1;
    motion.look(p, t, ms, millis());
    sendStatus();
  });
  http.on("/api/center", HTTP_POST, [] {
    motion.center(millis());
    sendStatus();
  });
  http.on("/api/sweep", HTTP_POST, [] {
    motion.startSweep(millis());
    sendStatus();
  });
  http.onNotFound([] { http.send(404, "application/json", "{\"error\":\"not found\"}"); });
  http.begin();
}

static void startAp() {
  if (apStarted) return;
  WiFi.mode(settings.ssid.length() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP("lynx-setup");  // open AP; set credentials via serial `wifi`
  apStarted = true;
  Serial.printf("SoftAP lynx-setup at %s\n", WiFi.softAPIP().toString().c_str());
}

static void pollWifi() {
  static wl_status_t last = WL_IDLE_STATUS;
  wl_status_t s = WiFi.status();
  if (s != last) {
    last = s;
    if (s == WL_CONNECTED) Serial.printf("wifi connected: http://%s/\n", WiFi.localIP().toString().c_str());
  }
  if (s != WL_CONNECTED && !apStarted && millis() - staStartedMs > kStaConnectTimeoutMs) startAp();
}

// ---------- entry points ----------

void setup() {
  Serial.begin(115200);
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  loadSettings(settings);
  motion.begin(settings.cal[PAN], settings.cal[TILT], settings.motion, millis());

  WiFi.setHostname("lynx");
  if (settings.ssid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());
    staStartedMs = millis();
  } else {
    startAp();
  }
  setupHttp();
  Serial.println("lynx-motion ready (type 'help')");
}

void loop() {
  uint32_t now = millis();
  pollSerial();
  http.handleClient();
  pollWifi();
  motion.update(now);
  driveServos();
}
