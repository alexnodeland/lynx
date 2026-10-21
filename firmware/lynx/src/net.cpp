// SPDX-License-Identifier: MIT
#include "net.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <atomic>

#include "app.h"
#include "netplan.h"

namespace lynx {

static const uint32_t kBootHoldMs = 3000;
static const uint32_t kSetupWindowMs = 600000;
static const int kBootPin = 0;  // the XIAO's BOOT button

static DNSServer dns;
static NetPlan plan;  // the open/close rules (lib/netplan, unit-tested)
static bool apUp = false;
static bool mdnsUp = false;
static bool linkUp = false;
// requests from other tasks (HTTP setup page), carried out by netLoop on the main task
static std::atomic<bool> joinPending{false};
static std::atomic<uint32_t> windowPending{0};

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

String deviceId() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char b[13];
  snprintf(b, sizeof b, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return b;
}
String staIp() { return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String(); }
IPAddress apIp() { return WiFi.softAPIP(); }
bool setupMode() { return apUp; }
uint32_t setupClosesInMs() { return plan.closesInMs(millis()); }

const char* wifiState() {
  switch (plan.join()) {
    case JoinState::Connecting: return "connecting";
    case JoinState::Connected: return "connected";
    case JoinState::Failed: return "failed";
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
  MDNS.addServiceTxt("lynx", "tcp", "id", deviceId().c_str());
  mdnsUp = true;
}

static void openAp() {
  if (apUp) return;
  WiFi.mode(settings.ssid.length() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(hostName().c_str());  // open: only reachable within radio range, and only while setting up
  dns.start(53, "*", WiFi.softAPIP());  // captive portal: every name resolves to us
  apUp = true;
  startMdns();
  Serial.printf("setup hotspot '%s' at http://%s/\n", hostName().c_str(), WiFi.softAPIP().toString().c_str());
}

static void closeAp() {
  if (!apUp) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  apUp = false;
  Serial.println("setup hotspot closed");
}

static void act(NetAction a) {
  if (a == NetAction::OpenAp) openAp();
  if (a == NetAction::CloseAp) closeAp();
}

void enterSetupMode(uint32_t ms) { windowPending = ms ? ms : kSetupWindowMs; }

void joinWifi(const String& ssid, const String& pass) {
  settings.ssid = ssid;
  settings.pass = pass;
  saveWifi(ssid, pass);
  joinPending = true;
}

void netBegin() {
  pinMode(kBootPin, INPUT_PULLUP);
  WiFi.persistent(false);  // credentials live in our own NVS namespace
  WiFi.setHostname(hostName().c_str());
  WiFi.setAutoReconnect(true);
  bool have = settings.ssid.length() > 0;
  if (have) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());
  }
  act(plan.begin(millis(), have));
}

void netLoop() {
  uint32_t now = millis();
  if (apUp) dns.processNextRequest();

  if (joinPending.exchange(false)) {
    WiFi.mode(apUp ? WIFI_AP_STA : WIFI_STA);
    WiFi.disconnect();
    WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());
    plan.joining(now);
    Serial.printf("joining '%s'\n", settings.ssid.c_str());
  }
  if (uint32_t w = windowPending.exchange(0)) act(plan.openWindow(now, w));

  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !linkUp) {
    WiFi.setSleep(false);  // modem sleep adds 100+ ms of latency to every stream frame
    // macOS and iOS ask mDNS for an IPv6 address first; without one they wait ~5 s before
    // falling back to IPv4, which makes lynx-xxxx.local feel dead. A link-local v6 address fixes it.
    WiFi.enableIpV6();
    startMdns();
    Serial.printf("joined: http://%s/ (http://%s/)\n", mdnsName().c_str(), WiFi.localIP().toString().c_str());
  }
  linkUp = up;
  act(plan.tick(now, up));

  // hold BOOT for 3 s to reopen the setup hotspot
  static uint32_t downSince = 0;
  if (digitalRead(kBootPin) == LOW) {
    if (!downSince) downSince = now ? now : 1;
    if (downSince != UINT32_MAX && now - downSince > kBootHoldMs) {
      act(plan.openWindow(now, kSetupWindowMs));
      downSince = UINT32_MAX;  // once per press
    }
  } else {
    downSince = 0;
  }
}

}  // namespace lynx
