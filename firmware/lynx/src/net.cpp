// SPDX-License-Identifier: MIT
#include "net.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_mac.h>

#include "app.h"

namespace lynx {

static const uint32_t kJoinTimeoutMs = 20000;
static const uint32_t kApLingerMs = 180000;  // after joining, keep the hotspot so the setup page can show the result
static const uint32_t kBootHoldMs = 3000;
static const uint32_t kSetupWindowMs = 600000;
static const int kBootPin = 0;  // the XIAO's BOOT button

static DNSServer dns;
static bool apUp = false;
static uint32_t apUntil = 0;  // 0 = until a network is joined
static enum { IDLE, CONNECTING, CONNECTED, FAILED } state = IDLE;
static uint32_t joinStart = 0;
static bool mdnsUp = false;

String hostName() {
  static String name;
  if (!name.length()) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char b[16];
    snprintf(b, sizeof b, "lynx-%02x%02x", mac[4], mac[5]);
    name = b;
  }
  return name;
}

String mdnsName() { return hostName() + ".local"; }
String staIp() { return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String(); }
IPAddress apIp() { return WiFi.softAPIP(); }
bool setupMode() { return apUp; }

const char* wifiState() {
  switch (state) {
    case CONNECTING: return "connecting";
    case CONNECTED: return "connected";
    case FAILED: return "failed";
    default: return "idle";
  }
}

static void startMdns() {
  if (mdnsUp || !MDNS.begin(hostName().c_str())) return;
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("lynx", "tcp", 80);
  MDNS.addServiceTxt("lynx", "tcp", "mcp", "/mcp");
  MDNS.addServiceTxt("lynx", "tcp", "stream", ":81/stream");
  MDNS.addServiceTxt("lynx", "tcp", "version", kVersion);
  mdnsUp = true;
}

static void startAp(uint32_t ms) {
  apUntil = ms ? millis() + ms : 0;
  if (apUp) return;
  WiFi.mode(settings.ssid.length() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(hostName().c_str());  // open: only reachable within radio range, and only while setting up
  dns.start(53, "*", WiFi.softAPIP());  // captive portal: every name resolves to us
  apUp = true;
  startMdns();
  Serial.printf("setup hotspot '%s' at http://%s/\n", hostName().c_str(), WiFi.softAPIP().toString().c_str());
}

static void stopAp() {
  if (!apUp) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apUp = false;
  Serial.println("setup hotspot closed");
}

void enterSetupMode(uint32_t ms) { startAp(ms ? ms : kSetupWindowMs); }

void joinWifi(const String& ssid, const String& pass) {
  settings.ssid = ssid;
  settings.pass = pass;
  saveWifi(ssid, pass);
  WiFi.mode(apUp ? WIFI_AP_STA : WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.c_str());
  state = CONNECTING;
  joinStart = millis();
  Serial.printf("joining '%s'\n", ssid.c_str());
}

void netBegin() {
  pinMode(kBootPin, INPUT_PULLUP);
  WiFi.persistent(false);  // credentials live in our own NVS namespace
  WiFi.setHostname(hostName().c_str());
  WiFi.setAutoReconnect(true);
  if (settings.ssid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // modem sleep adds 100+ ms of latency to every stream frame
    WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());
    state = CONNECTING;
    joinStart = millis();
  } else {
    startAp(0);
  }
}

void netLoop() {
  uint32_t now = millis();
  if (apUp) dns.processNextRequest();

  bool up = WiFi.status() == WL_CONNECTED;
  if (up && state != CONNECTED) {
    state = CONNECTED;
    WiFi.setSleep(false);
    startMdns();
    Serial.printf("joined: http://%s/ (http://%s/)\n", mdnsName().c_str(), WiFi.localIP().toString().c_str());
    if (apUp && apUntil == 0) apUntil = now + kApLingerMs;
  } else if (!up && state == CONNECTED) {
    state = CONNECTING;  // auto-reconnect is on
    joinStart = now;
  } else if (!up && state == CONNECTING && now - joinStart > kJoinTimeoutMs) {
    state = FAILED;
    if (!apUp) startAp(0);  // can't reach the saved network: offer setup again (and keep retrying it)
  }
  if (apUp && apUntil && (int32_t)(now - apUntil) > 0 && up) stopAp();

  // hold BOOT for 3 s to reopen the setup hotspot
  static uint32_t downSince = 0;
  if (digitalRead(kBootPin) == LOW) {
    if (!downSince) downSince = now ? now : 1;
    if (downSince != UINT32_MAX && now - downSince > kBootHoldMs) {
      enterSetupMode(kSetupWindowMs);
      downSince = UINT32_MAX;  // once per press
    }
  } else {
    downSince = 0;
  }
}

}  // namespace lynx
